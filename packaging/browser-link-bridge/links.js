"use strict";
(() => {
    const hosts = new Set(["soundcloud.com", "www.soundcloud.com", "m.soundcloud.com", "on.soundcloud.com"]);
    document.addEventListener("click", event => {
        if (!event.isTrusted || event.defaultPrevented || event.button !== 0 || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
        const anchor = event.composedPath().find(node => node instanceof HTMLAnchorElement);
        if (!anchor || anchor.hasAttribute("download")) return;
        let url;
        try { url = new URL(anchor.href); } catch { return; }
        if (!hosts.has(url.hostname) || !["https:", "http:"].includes(url.protocol) || url.username || url.password) return;
        // Keep OAuth, account and purchase flows in their browser context.
        if (/^\/(connect|authorize|oauth|signin|signout|login|logout|settings|you|upload|checkout|subscriptions|terms-of-use|pages|search|stream|charts|popular|notifications|messages)(\/|$)/i.test(url.pathname)) return;
        if (url.pathname === "/" || url.pathname === "" || (/^\/discover(\/|$)/i.test(url.pathname) && !/^\/discover\/sets\//i.test(url.pathname))) return;
        event.preventDefault();
        event.stopImmediatePropagation();
        window.location.assign("scrip://open?url=" + encodeURIComponent(url.href));
    }, true);
})();
