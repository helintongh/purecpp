window.MathJax = {
    loader: {
        paths: {
            fonts: 'script/mathjax-fonts'
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

window.renderMathInElement = function (element) {
    const target = element || document.body;
    if (!target || !window.MathJax || !window.MathJax.typesetPromise) {
        return Promise.resolve();
    }

    if (window.MathJax.typesetClear) {
        window.MathJax.typesetClear([target]);
    }

    return window.MathJax.typesetPromise([target]).catch((error) => {
        console.error('MathJax render failed:', error);
    });
};
