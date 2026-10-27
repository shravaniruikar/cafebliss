
#ifndef MODELS_H
#define MODELS_H

#include <string>
#include <vector>

using namespace std;

class MenuItem
{
private:
    int id;
    string name;
    string category;
    double price;

public:
    MenuItem(int id, string name, string category, double price)
    {
        this->id = id;
        this->name = name;
        this->category = category;
        this->price = price;
    }

    int getId() const
    {
        return id;
    }

    string getName() const
    {
        return name;
    }

    string getCategory() const
    {
        return category;
    }

    double getPrice() const
    {
        return price;
    }
};

class OrderItem
{
public:
    int itemId;
    string name;
    double price;
    int quantity;

    OrderItem(int itemId, string name, double price, int quantity)
    {
        this->itemId = itemId;
        this->name = name;
        this->price = price;
        this->quantity = quantity;
    }

    double getSubtotal() const
    {
        return price * quantity;
    }
};

class Order
{
private:
    int orderId;
    string customerName;
    vector<OrderItem> items;
    string status;

public:
    Order(int orderId, string customerName)
    {
        this->orderId = orderId;
        this->customerName = customerName;
        this->status = "Pending";
    }

    void addItem(OrderItem item)
    {
        items.push_back(item);
    }

    double calculateTotal() const
    {
        double total = 0;

        for (const auto &item : items)
        {
            total += item.getSubtotal();
        }

        return total;
    }

    int getOrderId() const
    {
        return orderId;
    }

    string getCustomerName() const
    {
        return customerName;
    }

    vector<OrderItem> getItems() const
    {
        return items;
    }

    string getStatus() const
    {
        return status;
    }

    void setStatus(string newStatus)
    {
        status = newStatus;
    }
};

#endif
