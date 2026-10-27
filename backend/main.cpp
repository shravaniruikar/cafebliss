
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "models.h"

using namespace std;
using json = nlohmann::json;

const string MENU_FILE = "data/menu.json";
const string ORDERS_FILE = "data/orders.json";

json readFile(const string &filename, const json &fallback);
bool writeFile(const string &filename, const json &data);

struct PendingPayment
{
    json order;
    int amount;
};

string getRazorpayKeyId()
{
    const char *value = getenv("RAZORPAY_KEY_ID");
    return value == nullptr ? "" : value;
}

string getRazorpayKeySecret()
{
    const char *value = getenv("RAZORPAY_KEY_SECRET");
    return value == nullptr ? "" : value;
}

bool hasTestCredentials()
{
    return getRazorpayKeyId().rfind("rzp_test_", 0) == 0 &&
           !getRazorpayKeySecret().empty();
}

int makeOrderId()
{
    return static_cast<int>(
        chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count()
        % 2000000000);
}

json buildOrder(const json &request)
{
    string customerName = request.at("name").get<string>();
    json requestedItems = request.at("items");

    if (customerName.find_first_not_of(" \t\r\n") == string::npos ||
        !requestedItems.is_array() || requestedItems.empty())
    {
        throw invalid_argument("Name and cart items are required");
    }

    json menu = readFile(MENU_FILE, json::array());
    Order order(makeOrderId(), customerName);

    for (const auto &requested : requestedItems)
    {
        int itemId = requested.at("id").get<int>();
        int quantity = requested.at("quantity").get<int>();

        if (quantity < 1 || quantity > 100)
        {
            throw invalid_argument("Invalid quantity");
        }

        bool found = false;
        for (const auto &item : menu)
        {
            if (item.at("id").get<int>() == itemId)
            {
                order.addItem(OrderItem(
                    itemId,
                    item.at("name").get<string>(),
                    item.at("price").get<double>(),
                    quantity));
                found = true;
                break;
            }
        }

        if (!found)
        {
            throw invalid_argument("Invalid menu item");
        }
    }

    json savedItems = json::array();
    for (const auto &item : order.getItems())
    {
        savedItems.push_back({
            {"id", item.itemId},
            {"name", item.name},
            {"price", item.price},
            {"quantity", item.quantity},
            {"subtotal", item.getSubtotal()}
        });
    }

    return {
        {"id", order.getOrderId()},
        {"name", order.getCustomerName()},
        {"items", savedItems},
        {"total", order.calculateTotal()},
        {"status", order.getStatus()}
    };
}

bool saveOrder(const json &order)
{
    json orders = readFile(ORDERS_FILE, json::array());
    orders.insert(orders.begin(), order);
    return writeFile(ORDERS_FILE, orders);
}

string hmacSha256(const string &message, const string &secret)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength = 0;
    if (HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char *>(message.data()),
             message.size(), digest, &digestLength) == nullptr)
    {
        return "";
    }

    static const char hex[] = "0123456789abcdef";
    string result;
    result.reserve(digestLength * 2);
    for (unsigned int i = 0; i < digestLength; ++i)
    {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return result;
}

// Read data from a JSON file.
json readFile(const string &filename, const json &fallback)
{
    ifstream file(filename);

    if (!file.is_open())
        return fallback;

    try
    {
        json data;
        file >> data;
        return data;
    }
    catch (...)
    {
        return fallback;
    }
}

// Save data to a JSON file.
bool writeFile(const string &filename, const json &data)
{
    ofstream file(filename);

    if (!file.is_open())
        return false;

    file << data.dump(4);
    return file.good();
}

int main()
{
    httplib::Server server;
    map<string, PendingPayment> pendingPayments;
    mutex paymentMutex;
    mutex ordersMutex;

    // Allow the frontend to communicate with the backend.
    server.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                                {"Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS"},
                                {"Access-Control-Allow-Headers", "Content-Type"}});

    server.Options(R"(.*)", [](const httplib::Request &req,
                               httplib::Response &res)
                   { res.status = 204; });

    // 1. Get menu items from menu.json.
    server.Get("/api/menu", [](const httplib::Request &req,
                               httplib::Response &res)
               {
        json menu = readFile(MENU_FILE, json::array());
        res.set_content(menu.dump(4), "application/json"); });

    server.Get("/api/payments/config", [](const httplib::Request &req,
                                          httplib::Response &res)
               {
        json config = {{"enabled", hasTestCredentials()}};
        if (hasTestCredentials())
        {
            config["keyId"] = getRazorpayKeyId();
        }
        res.set_content(config.dump(), "application/json"); });

    // Create an online order at Razorpay using prices loaded from menu.json.
    server.Post("/api/payments/create-order",
                [&pendingPayments, &paymentMutex](const httplib::Request &req,
                                                   httplib::Response &res)
                {
        if (!hasTestCredentials())
        {
            res.status = 503;
            res.set_content(
                R"({"error":"Online payments are unavailable. Add Razorpay test credentials or choose Cash at Counter."})",
                "application/json");
            return;
        }

        try
        {
            json request = json::parse(req.body);
            json order = buildOrder(request);
            int amount = static_cast<int>(llround(order.at("total").get<double>() * 100));

            httplib::SSLClient client("api.razorpay.com", 443);
            client.set_connection_timeout(5, 0);
            client.set_read_timeout(10, 0);
            client.set_write_timeout(10, 0);
            client.set_basic_auth(getRazorpayKeyId(), getRazorpayKeySecret());

            json razorpayRequest = {
                {"amount", amount},
                {"currency", "INR"},
                {"receipt", "cafe-bliss-" + to_string(order.at("id").get<int>())}
            };
            auto result = client.Post(
                "/v1/orders", razorpayRequest.dump(), "application/json");

            if (!result)
            {
                res.status = 502;
                res.set_content(
                    R"({"error":"Could not reach Razorpay. Please try again or choose Cash at Counter."})",
                    "application/json");
                return;
            }
            if (result->status < 200 || result->status >= 300)
            {
                res.status = 502;
                res.set_content(
                    R"({"error":"Razorpay could not create the payment. Check your test credentials and try again."})",
                    "application/json");
                return;
            }

            json razorpayOrder = json::parse(result->body);
            string razorpayOrderId = razorpayOrder.at("id").get<string>();
            if (razorpayOrder.at("amount").get<int>() != amount ||
                razorpayOrder.at("currency").get<string>() != "INR")
            {
                res.status = 502;
                res.set_content(
                    R"({"error":"Razorpay returned an unexpected order amount."})",
                    "application/json");
                return;
            }

            {
                lock_guard<mutex> lock(paymentMutex);
                pendingPayments[razorpayOrderId] = {order, amount};
            }

            res.status = 201;
            res.set_content(json({
                {"keyId", getRazorpayKeyId()},
                {"razorpayOrderId", razorpayOrderId},
                {"amount", amount},
                {"currency", "INR"}
            }).dump(), "application/json");
        }
        catch (const invalid_argument &error)
        {
            res.status = 400;
            res.set_content(json({{"error", error.what()}}).dump(),
                            "application/json");
        }
        catch (const exception &)
        {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid online payment request"})",
                "application/json");
        } });

    // Save an online order only after its Razorpay signature is verified.
    server.Post("/api/payments/verify",
                [&pendingPayments, &paymentMutex, &ordersMutex](
                    const httplib::Request &req, httplib::Response &res)
                {
        if (!hasTestCredentials())
        {
            res.status = 503;
            res.set_content(
                R"({"error":"Online payments are unavailable. Choose Cash at Counter."})",
                "application/json");
            return;
        }

        try
        {
            json request = json::parse(req.body);
            string razorpayOrderId =
                request.at("razorpay_order_id").get<string>();
            string paymentId = request.at("razorpay_payment_id").get<string>();
            string signature = request.at("razorpay_signature").get<string>();
            string expectedSignature = hmacSha256(
                razorpayOrderId + "|" + paymentId, getRazorpayKeySecret());

            if (expectedSignature.empty() ||
                signature.size() != expectedSignature.size() ||
                CRYPTO_memcmp(signature.data(), expectedSignature.data(),
                              expectedSignature.size()) != 0)
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Payment verification failed. The order was not saved as paid."})",
                    "application/json");
                return;
            }

            lock_guard<mutex> paymentLock(paymentMutex);
            auto pending = pendingPayments.find(razorpayOrderId);
            if (pending == pendingPayments.end())
            {
                lock_guard<mutex> ordersLock(ordersMutex);
                json orders = readFile(ORDERS_FILE, json::array());
                for (const auto &savedOrder : orders)
                {
                    if (savedOrder.value("razorpayOrderId", string()) ==
                            razorpayOrderId &&
                        savedOrder.value("razorpayPaymentId", string()) ==
                            paymentId &&
                        savedOrder.value("paymentStatus", string()) == "Paid")
                    {
                        res.set_content(savedOrder.dump(4), "application/json");
                        return;
                    }
                }

                res.status = 400;
                res.set_content(
                    R"({"error":"Payment order was not found or was already verified."})",
                    "application/json");
                return;
            }

            json order = pending->second.order;
            if (pending->second.amount <= 0 ||
                llround(order.at("total").get<double>() * 100) !=
                    pending->second.amount)
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Payment amount verification failed."})",
                    "application/json");
                return;
            }

            order["paymentMethod"] = "Online";
            order["paymentStatus"] = "Paid";
            order["razorpayOrderId"] = razorpayOrderId;
            order["razorpayPaymentId"] = paymentId;

            lock_guard<mutex> ordersLock(ordersMutex);
            if (!saveOrder(order))
            {
                res.status = 500;
                res.set_content(
                    R"({"error":"Payment was verified, but the order could not be saved. Please contact the cafe with your payment ID."})",
                    "application/json");
                return;
            }

            pendingPayments.erase(pending);
            res.status = 201;
            res.set_content(order.dump(4), "application/json");
            cout << "Paid online order saved: #"
                 << order.at("id").get<int>() << endl;
        }
        catch (const exception &)
        {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid payment verification request"})",
                "application/json");
        } });

    // 2. Place a cash-at-counter order.
    server.Post("/api/orders", [&ordersMutex](const httplib::Request &req,
                                              httplib::Response &res)
                {
        try
        {
            json request = json::parse(req.body);
            if (request.contains("paymentMethod") &&
                request.at("paymentMethod").get<string>() != "Cash")
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Use the online payment checkout for online orders."})",
                    "application/json");
                return;
            }

            json savedOrder = buildOrder(request);
            savedOrder["paymentMethod"] = "Cash";
            savedOrder["paymentStatus"] = "Pending";

            lock_guard<mutex> lock(ordersMutex);
            if (!saveOrder(savedOrder))
            {
                res.status = 500;
                res.set_content(
                    R"({"error":"Could not save order"})",
                    "application/json");
                return;
            }

            res.status = 201;
            res.set_content(savedOrder.dump(4), "application/json");

            cout << "Order saved: #" << savedOrder.at("id").get<int>()
                 << " | Total: Rs. "
                 << savedOrder.at("total").get<double>() << endl;
        }
        catch (const invalid_argument &error)
        {
            res.status = 400;
            res.set_content(json({{"error", error.what()}}).dump(),
                            "application/json");
        }
        catch (const exception &)
        {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid order request"})",
                "application/json");
        } });

    // 3. Get order history.
    server.Get("/api/orders", [&ordersMutex](const httplib::Request &req,
                                             httplib::Response &res)
               {
        lock_guard<mutex> lock(ordersMutex);
        json orders = readFile(ORDERS_FILE, json::array());
        res.set_content(orders.dump(4), "application/json"); });

    // 4. Update an order status.
    server.Put(R"(/api/orders/(\d+)/status)",
               [&ordersMutex](const httplib::Request &req,
                  httplib::Response &res)
               {
                   try
                   {
                       int id = stoi(req.matches[1]);

                       json body = json::parse(req.body);
                       string newStatus =
                           body.at("status").get<string>();

                       // Allow only these three statuses.
                       if (newStatus != "Pending" &&
                           newStatus != "Preparing" &&
                           newStatus != "Ready")
                       {
                           res.status = 400;
                           res.set_content(
                               R"({"error":"Invalid status"})",
                               "application/json");
                           return;
                       }

                       lock_guard<mutex> lock(ordersMutex);
                       json orders = readFile(ORDERS_FILE, json::array());

                       for (auto &order : orders)
                       {
                           if (order.at("id").get<int>() == id)
                           {
                               order["status"] = newStatus;

                               if (!writeFile(ORDERS_FILE, orders))
                               {
                                   res.status = 500;
                                   res.set_content(
                                       R"({"error":"Could not save status"})",
                                       "application/json");
                                   return;
                               }

                               res.set_content(
                                   order.dump(4), "application/json");

                               cout << "Order #" << id
                                    << " status updated to "
                                    << newStatus << endl;
                               return;
                           }
                       }

                       res.status = 404;
                       res.set_content(
                           R"({"error":"Order not found"})",
                           "application/json");
                   }
                   catch (...)
                   {
                       res.status = 400;
                       res.set_content(
                           R"({"error":"Invalid request"})",
                           "application/json");
                   }
               });

    // Start the server.
    cout << "Cafe Bliss backend running at "
         << "http://localhost:8080" << endl;

    if (!server.listen("127.0.0.1", 8080))
    {
        cerr << "Could not start server. "
             << "Is port 8080 already in use?" << endl;
        return 1;
    }

    return 0;
}
