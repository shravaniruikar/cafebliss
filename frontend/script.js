const API = "http://localhost:8080/api";

const menuPhotos = {
    1: "https://images.unsplash.com/photo-1495474472287-4d71bcdd2085?auto=format&fit=crop&w=800&q=85",
    2: "https://images.unsplash.com/photo-1461023058943-07fcbe16d735?auto=format&fit=crop&w=800&q=85",
    3: "https://images.unsplash.com/photo-1517701604599-bb29b565090c?auto=format&fit=crop&w=800&q=85",
    4: "https://images.unsplash.com/photo-1606313564200-e75d5e30476c?auto=format&fit=crop&w=800&q=85",
    5: "https://images.unsplash.com/photo-1528735602780-2552fd46c7af?auto=format&fit=crop&w=800&q=85",
    6: "https://images.unsplash.com/photo-1573080496219-bb080dd4f877?auto=format&fit=crop&w=800&q=85"
};

let menu = [];
let cart = [];
let orders = [];
let activeCategory = "All";
let onlinePaymentsEnabled = false;
let pendingPaymentVerification = null;

function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, character => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"
    })[character]);
}

async function requestJson(url, options) {
    const response = await fetch(url, options);
    let result;
    try {
        result = await response.json();
    } catch {
        throw new Error("The server returned an unreadable response. Please try again.");
    }

    if (!response.ok) {
        throw new Error(result.error || "The request could not be completed.");
    }
    return result;
}

async function loadMenu() {
    const container = document.getElementById("menu-items");
    try {
        menu = await requestJson(`${API}/menu`);
        displayMenu();
    } catch {
        container.innerHTML = '<p class="empty-state">Cannot connect to the Cafe Bliss server. Please start the backend and refresh.</p>';
    }
}

function displayMenu() {
    const container = document.getElementById("menu-items");
    const visibleItems = activeCategory === "All"
        ? menu
        : menu.filter(item => {
            const category = item.category === "Dessert" ? "Desserts" : item.category;
            return category === activeCategory;
        });

    if (visibleItems.length === 0) {
        container.innerHTML = '<p class="empty-state">There are no items in this category yet.</p>';
        return;
    }

    container.innerHTML = visibleItems.map(item => `
        <article class="menu-card">
            <div class="menu-photo-wrap">
                <img class="menu-photo" src="${menuPhotos[item.id] || menuPhotos[1]}" alt="${escapeHtml(item.name)}" loading="lazy">
                <span class="menu-category">${escapeHtml(item.category)}</span>
            </div>
            <div class="menu-card-content">
                <div class="menu-card-title">
                    <h3>${escapeHtml(item.name)}</h3>
                    <span class="price">₹${Number(item.price).toFixed(0)}</span>
                </div>
                <p class="menu-description">${item.category === "Coffee" ? "A cafe favourite, freshly made just for you." : item.category === "Food" ? "A satisfying bite, prepared fresh to order." : "A little something sweet for your day."}</p>
                <button class="add-btn" type="button" onclick="addToCart(${Number(item.id)})">Add to cart <span aria-hidden="true">+</span></button>
            </div>
        </article>
    `).join("");
}

function addToCart(id) {
    const item = menu.find(product => Number(product.id) === id);
    if (!item) return;

    const existing = cart.find(product => product.id === id);
    if (existing) {
        existing.quantity++;
    } else {
        cart.push({
            id: Number(item.id),
            name: item.name,
            price: Number(item.price),
            quantity: 1
        });
    }
    displayCart();
}

function changeQuantity(id, change) {
    const item = cart.find(product => product.id === id);
    if (!item) return;

    item.quantity += change;
    if (item.quantity <= 0) {
        cart = cart.filter(product => product.id !== id);
    }
    displayCart();
}

function displayCart() {
    const container = document.getElementById("cart-items");
    const count = cart.reduce((sum, item) => sum + item.quantity, 0);
    const total = cart.reduce((sum, item) => sum + item.price * item.quantity, 0);

    document.getElementById("cart-count").textContent = count;
    document.getElementById("total").textContent = total.toFixed(0);

    if (cart.length === 0) {
        container.innerHTML = '<p class="empty-state">Your next favourite is waiting in the menu.</p>';
        return;
    }

    container.innerHTML = cart.map(item => `
        <div class="cart-row">
            <div class="cart-item-name"><strong>${escapeHtml(item.name)}</strong><small>₹${item.price.toFixed(0)} each</small></div>
            <div class="quantity-control" aria-label="${escapeHtml(item.name)} quantity">
                <button type="button" aria-label="Remove one ${escapeHtml(item.name)}" onclick="changeQuantity(${item.id}, -1)">−</button>
                <span>${item.quantity}</span>
                <button type="button" aria-label="Add one ${escapeHtml(item.name)}" onclick="changeQuantity(${item.id}, 1)">+</button>
            </div>
            <strong class="cart-line-total">₹${(item.price * item.quantity).toFixed(0)}</strong>
        </div>
    `).join("");
}

async function placeCashOrder(name) {
    return requestJson(`${API}/orders`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            name,
            paymentMethod: "Cash",
            items: cart.map(item => ({ id: item.id, quantity: item.quantity }))
        })
    });
}

async function placeOnlineOrder(name) {
    if (!onlinePaymentsEnabled) {
        throw new Error("Online payments are unavailable. Please choose Cash at Counter.");
    }
    if (!window.Razorpay) {
        throw new Error("Razorpay Checkout could not be loaded. Check your internet connection or choose Cash at Counter.");
    }

    const checkoutOrder = await requestJson(`${API}/payments/create-order`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            name,
            items: cart.map(item => ({ id: item.id, quantity: item.quantity }))
        })
    });

    return new Promise((resolve, reject) => {
        let completed = false;
        const finish = (callback, value) => {
            if (completed) return;
            completed = true;
            callback(value);
        };

        const checkout = new window.Razorpay({
            key: checkoutOrder.keyId,
            amount: checkoutOrder.amount,
            currency: checkoutOrder.currency,
            name: "Cafe Bliss",
            description: "Your Cafe Bliss order",
            order_id: checkoutOrder.razorpayOrderId,
            prefill: { name },
            theme: { color: "#70442d" },
            handler: async payment => {
                try {
                    const savedOrder = await requestJson(`${API}/payments/verify`, {
                        method: "POST",
                        headers: { "Content-Type": "application/json" },
                        body: JSON.stringify(payment)
                    });
                    pendingPaymentVerification = null;
                    document.getElementById("retry-verification").hidden = true;
                    finish(resolve, savedOrder);
                } catch (error) {
                    pendingPaymentVerification = payment;
                    document.getElementById("retry-verification").hidden = false;
                    finish(reject, new Error(
                        `Payment was submitted, but Cafe Bliss could not confirm it. Retry payment confirmation below; do not make another payment. Payment ID: ${payment.razorpay_payment_id}. ${error.message}`
                    ));
                }
            },
            modal: {
                ondismiss: () => finish(reject, new Error("Payment was cancelled. No order was placed."))
            }
        });

        checkout.on("payment.failed", payment => {
            const reason = payment.error && payment.error.description
                ? `: ${payment.error.description}`
                : ".";
            finish(reject, new Error(`Payment failed${reason} No order was placed.`));
        });

        checkout.open();
    });
}

async function placeOrder(event) {
    event.preventDefault();
    const name = document.getElementById("customer-name").value.trim();
    const message = document.getElementById("message");
    const button = document.getElementById("place-order");
    const paymentMethod = document.querySelector('input[name="payment-method"]:checked').value;

    if (!name) {
        message.textContent = "Please enter your name.";
        return;
    }
    if (cart.length === 0) {
        message.textContent = "Please add items to your cart.";
        return;
    }

    button.disabled = true;
    message.textContent = paymentMethod === "Online"
        ? "Opening secure Razorpay checkout…"
        : "Placing your order…";

    try {
        const result = paymentMethod === "Online"
            ? await placeOnlineOrder(name)
            : await placeCashOrder(name);

        cart = [];
        displayCart();
        message.textContent = paymentMethod === "Online"
            ? `Payment confirmed. Order #${result.id} is paid. Total: ₹${Number(result.total).toFixed(0)}.`
            : `Order #${result.id} placed. Pay ₹${Number(result.total).toFixed(0)} at the counter.`;
        await loadOrders();
    } catch (error) {
        message.textContent = error.message || "Order could not be placed. Please try again.";
    } finally {
        button.disabled = false;
    }
}

async function retryPaymentVerification() {
    if (!pendingPaymentVerification) return;

    const retryButton = document.getElementById("retry-verification");
    const placeOrderButton = document.getElementById("place-order");
    const message = document.getElementById("message");
    retryButton.disabled = true;
    placeOrderButton.disabled = true;
    message.textContent = "Retrying payment confirmation. No new payment will be started…";

    try {
        const order = await requestJson(`${API}/payments/verify`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(pendingPaymentVerification)
        });
        pendingPaymentVerification = null;
        retryButton.hidden = true;
        cart = [];
        displayCart();
        message.textContent = `Payment confirmed. Order #${order.id} is paid. Total: ₹${Number(order.total).toFixed(0)}.`;
        await loadOrders();
    } catch (error) {
        message.textContent = `Payment confirmation is still pending. Do not make another payment. ${error.message}`;
    } finally {
        retryButton.disabled = false;
        placeOrderButton.disabled = false;
    }
}

async function loadPaymentConfig() {
    const note = document.getElementById("payment-note");
    const onlineOption = document.getElementById("pay-online");

    try {
        const config = await requestJson(`${API}/payments/config`);
        onlinePaymentsEnabled = config.enabled === true && Boolean(config.keyId);
        onlineOption.disabled = !onlinePaymentsEnabled;
    } catch {
        onlinePaymentsEnabled = false;
        onlineOption.disabled = true;
    }

    if (!onlinePaymentsEnabled) {
        note.textContent = "Online checkout is unavailable until Razorpay test credentials are configured. Cash at Counter is ready to use.";
    } else {
        note.textContent = "Online payments use Razorpay test mode.";
    }
}

async function loadOrders() {
    const container = document.getElementById("order-history");
    try {
        orders = await requestJson(`${API}/orders`);
        displayOrders();
    } catch {
        container.innerHTML = '<p class="empty-state">Could not load order history. Please check the server connection.</p>';
    }
}

function displayOrders() {
    const container = document.getElementById("order-history");
    if (orders.length === 0) {
        container.innerHTML = '<p class="empty-state">Your Cafe Bliss orders will show up here.</p>';
        return;
    }

    container.innerHTML = orders.map(order => `
        <article class="order-row">
            <div class="order-topline">
                <strong>Order #${Number(order.id)}</strong>
                <span class="status-badge status-${escapeHtml(order.status).toLowerCase()}">${escapeHtml(order.status)}</span>
            </div>
            <p class="order-customer">For ${escapeHtml(order.name)}</p>
            <p class="order-items">${order.items.map(item =>
                `${escapeHtml(item.name)} × ${Number(item.quantity)}`
            ).join(", ")}</p>
            <div class="order-details">
                <span>${escapeHtml(order.paymentMethod || "Payment not recorded")} · ${escapeHtml(order.paymentStatus || "—")}</span>
                <strong>₹${Number(order.total).toFixed(0)}</strong>
            </div>
            <label class="status-label" for="status-${Number(order.id)}">Update order status</label>
            <select id="status-${Number(order.id)}" onchange="updateOrderStatus(${Number(order.id)}, this.value)">
                <option value="Pending" ${order.status === "Pending" ? "selected" : ""}>Pending</option>
                <option value="Preparing" ${order.status === "Preparing" ? "selected" : ""}>Preparing</option>
                <option value="Ready" ${order.status === "Ready" ? "selected" : ""}>Ready</option>
            </select>
        </article>
    `).join("");
}

async function updateOrderStatus(id, status) {
    try {
        await requestJson(`${API}/orders/${id}/status`, {
            method: "PUT",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ status })
        });
        await loadOrders();
    } catch (error) {
        window.alert(error.message);
        await loadOrders();
    }
}

document.getElementById("checkout-form").addEventListener("submit", placeOrder);
document.getElementById("retry-verification")
    .addEventListener("click", retryPaymentVerification);
document.querySelectorAll(".filter-btn").forEach(button => {
    button.addEventListener("click", () => {
        activeCategory = button.dataset.category;
        document.querySelectorAll(".filter-btn").forEach(filter => {
            const active = filter === button;
            filter.classList.toggle("active", active);
            filter.setAttribute("aria-pressed", String(active));
        });
        displayMenu();
    });
});

displayCart();
loadMenu();
loadOrders();
loadPaymentConfig();
