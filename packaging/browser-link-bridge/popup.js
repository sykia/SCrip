"use strict";
(async () => {
    const api = typeof browser !== "undefined" ? browser : chrome;
    const [tab] = await api.tabs.query({active: true, currentWindow: true});
    let url;
    try { url = new URL(tab?.url); } catch { return; }
    if (!["soundcloud.com", "www.soundcloud.com", "m.soundcloud.com", "on.soundcloud.com"].includes(url.hostname) || !["https:", "http:"].includes(url.protocol) || url.username || url.password) return;
    if (/^\/(connect|authorize|oauth|signin|signout|login|logout|settings|you|upload|checkout|subscriptions|terms-of-use|pages|search|stream|charts|popular|notifications|messages)(\/|$)/i.test(url.pathname) || url.pathname === "/" || (/^\/discover(\/|$)/i.test(url.pathname) && !/^\/discover\/sets\//i.test(url.pathname))) return;
    const link = document.getElementById("open");
    link.href = "scrip://open?url=" + encodeURIComponent(url.href);
    link.hidden = false;
    document.getElementById("message").textContent = "Открыть текущую страницу в приложении.";
})();
