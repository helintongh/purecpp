const mathJaxConfigScript = document.currentScript;
const mathJaxScriptBase = mathJaxConfigScript ? new URL('.', mathJaxConfigScript.src).href : 'script/';
const mathJaxFontBase = new URL('mathjax-fonts', mathJaxScriptBase).href.replace(/\/$/, '');

window.MathJax = {
    loader: {
        paths: {
            fonts: mathJaxFontBase
        }
    },
    tex: {
        inlineMath: [['$', '$'], ['\\(', '\\)']],
        displayMath: [['$$', '$$'], ['\\[', '\\]']],
        processEscapes: true,
        processEnvironments: true
    },
    options: {
        skipHtmlTags: ['script', 'noscript', 'style', 'textarea', 'pre', 'code'],
        ignoreHtmlClass: 'tex2jax_ignore',
        processHtmlClass: 'tex2jax_process'
    },
    startup: {
        typeset: false
    }
};

(function () {
    const renderKeyAttribute = 'data-mathjax-render-key';
    const pendingRenders = new WeakMap();

    function hashString(value) {
        let hash = 0;
        const text = String(value || '');
        for (let i = 0; i < text.length; i++) {
            hash = ((hash << 5) - hash) + text.charCodeAt(i);
            hash |= 0;
        }
        return String(hash);
    }

    window.getRenderKey = hashString;

    function waitForMathJax() {
        if (window.MathJax && window.MathJax.startup && window.MathJax.startup.promise && window.MathJax.typesetPromise) {
            return window.MathJax.startup.promise.then(() => window.MathJax);
        }

        return new Promise((resolve) => {
            const startedAt = Date.now();
            const wait = () => {
                if (window.MathJax && window.MathJax.startup && window.MathJax.startup.promise && window.MathJax.typesetPromise) {
                    window.MathJax.startup.promise.then(() => resolve(window.MathJax));
                    return;
                }

                if (Date.now() - startedAt > 10000) {
                    resolve(null);
                    return;
                }

                requestAnimationFrame(wait);
            };

            wait();
        });
    }

    window.sanitizeHtml = function (html) {
        if (!window.DOMPurify) {
            const fallback = document.createElement('div');
            fallback.textContent = html || '';
            return fallback.innerHTML;
        }

        return window.DOMPurify.sanitize(html || '', {
            ADD_ATTR: ['target']
        });
    };

    window.renderMarkdown = function (markdown) {
        const html = marked.parse(markdown || '');
        return window.sanitizeHtml(html);
    };

    window.renderMathInElement = function (element, options = {}) {
        const target = element || document.body;
        if (!target) {
            return Promise.resolve();
        }

        const renderKey = options.key || hashString(target.textContent || target.innerHTML || '');
        if (!options.force && target.getAttribute(renderKeyAttribute) === renderKey) {
            return pendingRenders.get(target) || Promise.resolve();
        }

        const renderPromise = waitForMathJax()
            .then((mathJax) => {
                if (!mathJax) {
                    return;
                }

                if (options.clear && mathJax.typesetClear) {
                    mathJax.typesetClear([target]);
                }

                return mathJax.typesetPromise([target]);
            })
            .then(() => {
                target.setAttribute(renderKeyAttribute, renderKey);
            })
            .catch((error) => {
                console.error('MathJax render failed:', error);
            })
            .finally(() => {
                pendingRenders.delete(target);
            });

        pendingRenders.set(target, renderPromise);
        return renderPromise;
    };

    window.renderMarkdownAndMath = function (element, markdown, options = {}) {
        if (!element) {
            return Promise.resolve();
        }

        const source = markdown || '';
        const renderKey = options.key || hashString(source);
        if (!options.force && element.getAttribute(renderKeyAttribute) === renderKey) {
            return Promise.resolve();
        }

        element.innerHTML = window.renderMarkdown(source);
        if (typeof options.afterRender === 'function') {
            options.afterRender(element);
        }

        return window.renderMathInElement(element, {
            key: renderKey,
            force: true,
            clear: options.clear === true
        });
    };

    window.renderMarkdownPreview = function (markdown, preview) {
        const html = window.renderMarkdown(markdown || '');
        if (preview) {
            const renderKey = hashString(markdown || '');
            queueMicrotask(() => window.renderMathInElement(preview, { key: renderKey }));
        }
        return html;
    };
}());
