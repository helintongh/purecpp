// layout.js - 统一布局脚本，包含用户信息管理功能

// 全局表情列表
const commonEmojis = [
    '😀', '😃', '😄', '😁', '😆', '😅', '😂', '🤣',
    '😊', '😇', '🙂', '🙃', '😉', '😌', '😍', '🥰',
    '😘', '😗', '😙', '😚', '😋', '😛', '😝', '😜',
    '🤪', '🤨', '🧐', '🤓', '😎', '🤩', '🥳', '😏',
    '😒', '😞', '😔', '😟', '😕', '🙁', '☹️', '😣',
    '😖', '😫', '😩', '🥺', '😢', '😭', '😤', '😠',
    '😡', '🤬', '🤯', '😳', '🥵', '🥶', '😱', '😨',
    '😰', '😥', '😓', '🤗', '🤔', '🤭', '🤫', '🤥',
    '😶', '😐', '😑', '😬', '🙄', '😯', '😦', '😧',
    '😮', '😲', '🥱', '😴', '🤤', '😪', '😵', '🤐',
    '🥴', '🤢', '🤮', '🤧', '😷', '🤒', '🤕', '🤑',
    '🤠', '😈', '👿', '👹', '👺', '🤡', '💩', '👻',
    '💀', '☠️', '👽', '👾', '🤖', '🎃', '😺', '😸',
    '😹', '😻', '😼', '😽', '🙀', '😿', '😾', '👋',
    '🤚', '🖐️', '🖖', '👌', '🤌', '🤏', '✌️', '🤞',
    '🤟', '🤘', '🤙', '👈', '👉', '👆', '👇', '☝️'
];

// DOM加载完成后执行
document.addEventListener('DOMContentLoaded', function () {
    // 初始化主题（在DOM加载完成后立即执行，不等待header和footer加载）
    initTheme();

    // 加载页眉
    loadHeader();

    // 加载页脚
    loadFooter();

    // 初始化通用功能
    initCommonFunctions();
});

// 加载页眉
function loadHeader() {
    fetch('header.html')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(html => {
            const headerContainer = document.getElementById('header-container');
            if (headerContainer) {
                headerContainer.innerHTML = html;
                // 确保登录状态的用户图标能正确显示
                if (typeof initUserInfo === 'function') {
                    initUserInfo();
                }
            }
        })
        .catch(error => {
            console.error('Error loading header:', error);
        });
}

// 加载页脚
function loadFooter() {
    fetch('footer.html')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.text();
        })
        .then(html => {
            const footerContainer = document.getElementById('footer-container');
            if (footerContainer) {
                footerContainer.innerHTML = html;
                // 页脚加载完成后重新初始化主题切换功能，确保事件绑定正确
                initTheme();
            }
        })
        .catch(error => {
            console.error('Error loading footer:', error);
        });
}

// 初始化通用功能
function initCommonFunctions() {
    // 移动端菜单切换逻辑
    const mobileToggle = document.querySelector('.mobile-toggle');
    if (mobileToggle) {
        mobileToggle.addEventListener('click', toggleMenu);
    }
}

// 主题切换功能
const THEME_STORAGE_KEY = 'purecpp_theme';
const htmlRoot = document.documentElement;

// 初始化主题
function initTheme() {
    const themeToggleBtn = document.getElementById('themeToggleBtn');
    if (themeToggleBtn) {
        // 检查用户是否有手动设置的主题
        const savedTheme = localStorage.getItem(THEME_STORAGE_KEY);

        // 如果有手动设置，使用用户设置；否则使用操作系统主题
        const isDarkMode = savedTheme === 'dark' || (!savedTheme && window.matchMedia('(prefers-color-scheme: dark)').matches);
        themeToggleBtn.checked = isDarkMode;

        // 确保只应用一个主题类
        htmlRoot.classList.remove('dark', 'light');
        htmlRoot.classList.add(isDarkMode ? 'dark' : 'light');

        // 确保事件监听器只绑定一次
        if (!themeToggleBtn.hasAttribute('data-event-bound')) {
            // 绑定主题切换事件
            themeToggleBtn.addEventListener('change', toggleTheme);
            themeToggleBtn.setAttribute('data-event-bound', 'true');
        }

        // 确保系统主题变化事件监听器只添加一次
        if (!window.__systemThemeListenerAdded) {
            // 添加监听操作系统主题变化的事件
            if (window.matchMedia) {
                const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
                mediaQuery.addEventListener('change', handleSystemThemeChange);
                window.__systemThemeListenerAdded = true;
            }
        }
    }
}

// 处理操作系统主题变化
function handleSystemThemeChange(e) {
    const isDarkMode = e.matches;
    const themeToggleBtn = document.getElementById('themeToggleBtn');
    if (themeToggleBtn) {
        // 无论用户是否有手动设置，都跟随系统主题变化
        themeToggleBtn.checked = isDarkMode;

        // 更新主题类
        htmlRoot.classList.remove('dark', 'light');
        htmlRoot.classList.add(isDarkMode ? 'dark' : 'light');

        // 更新localStorage中的主题设置
        localStorage.setItem(THEME_STORAGE_KEY, isDarkMode ? 'dark' : 'light');
    }
}

// 切换主题
function toggleTheme() {
    const themeToggleBtn = document.getElementById('themeToggleBtn');
    if (themeToggleBtn) {
        const isDarkMode = themeToggleBtn.checked;

        // 确保只应用一个主题类
        htmlRoot.classList.remove('dark', 'light');
        htmlRoot.classList.add(isDarkMode ? 'dark' : 'light');

        // 存储用户选择的主题
        localStorage.setItem(THEME_STORAGE_KEY, isDarkMode ? 'dark' : 'light');
    }
}

// 移动端菜单切换
function toggleMenu() {
    const navMenu = document.getElementById('nav-menu');
    if (navMenu) {
        navMenu.classList.toggle('active');

        // 切换图标
        const icon = document.querySelector('.mobile-toggle i');
        if (icon) {
            if (navMenu.classList.contains('active')) {
                icon.classList.remove('fa-bars');
                icon.classList.add('fa-times');
            } else {
                icon.classList.remove('fa-times');
                icon.classList.add('fa-bars');
            }
        }
    }
}

// 用户信息显示逻辑统一封装

// 检查用户是否登录
function checkUserLogin() {
    if (typeof apiService === 'undefined') {
        const loginLink = document.getElementById('login-link');
        const registerLink = document.getElementById('register-link');
        const userInfoElement = document.getElementById('user-info');
        if (loginLink) loginLink.style.display = 'block';
        if (registerLink) registerLink.style.display = 'block';
        if (userInfoElement) userInfoElement.style.display = 'none';
        return;
    }

    // 使用apiService获取用户信息和token
    const userInfo = apiService.getUserInfo();
    const token = apiService.getAccessToken();

    // 获取DOM元素
    const loginLink = document.getElementById('login-link');
    const registerLink = document.getElementById('register-link');
    const userInfoElement = document.getElementById('user-info');
    const admin_review_link = document.getElementById('admin_review_link');

    if (userInfo && token) {
        // 用户已登录，隐藏登录/注册链接，显示用户信息图标
        if (loginLink) loginLink.style.display = 'none';
        if (registerLink) registerLink.style.display = 'none';
        if (userInfoElement) userInfoElement.style.display = 'block';
        // 如果用户是管理员，显示审核管理链接
        if (userInfo.role === 'admin' || userInfo.role === 'superadmin') {
            if (admin_review_link) admin_review_link.style.display = 'block';
        } else {
            if (admin_review_link) admin_review_link.style.display = 'none';
        }

        // 更新用户菜单中的信息
        const usernameElement = document.getElementById('user-menu-username');
        if (usernameElement) usernameElement.textContent = "你好! " + userInfo.username;

        // 更新用户头像
        const userAvatarElement = document.getElementById('user-avatar');
        if (userAvatarElement) {
            userAvatarElement.src = userInfo.avatar;
        }
    } else {
        // 用户未登录，显示登录/注册链接，隐藏用户信息图标
        if (loginLink) loginLink.style.display = 'block';
        if (registerLink) registerLink.style.display = 'block';
        if (userInfoElement) userInfoElement.style.display = 'none';
    }
}

// 处理用户菜单的显示和隐藏
function initUserMenu() {
    const userMenuButton = document.getElementById('user-menu-button');
    const userMenu = document.getElementById('user-menu');

    if (!userMenuButton || !userMenu) return;

    userMenuButton.addEventListener('click', function () {
        userMenu.style.display = userMenu.style.display === 'block' ? 'none' : 'block';
    });

    // 点击页面其他地方关闭菜单
    document.addEventListener('click', function (event) {
        if (!userMenuButton.contains(event.target) && !userMenu.contains(event.target)) {
            userMenu.style.display = 'none';
        }
    });

    // 处理退出登录
    const logoutLink = document.getElementById('logout-link');
    if (logoutLink) {
        logoutLink.addEventListener('click', async function (e) {
            e.preventDefault();

            // 使用apiService处理登出
            try {
                if (typeof apiService !== 'undefined') {
                    await apiService.logout();
                }
            } catch (error) {
                console.error('Logout request failed:', error);
            } finally {
                // 刷新页面
                window.location.reload();
            }
        });
    }
}

// 添加用户菜单项的悬停效果
function addUserMenuHoverEffects() {
    const userMenuItems = document.querySelectorAll('.user-menu-item');
    userMenuItems.forEach(item => {
        item.addEventListener('mouseenter', function () {
            this.style.backgroundColor = 'rgba(0, 0, 0, 0.05)';
        });
        item.addEventListener('mouseleave', function () {
            this.style.backgroundColor = 'transparent';
        });
    });
}

// 添加用户菜单按钮的悬停效果
function addUserButtonHoverEffect() {
    const userMenuButton = document.getElementById('user-menu-button');
    if (!userMenuButton) return;

    userMenuButton.addEventListener('mouseenter', function () {
        this.style.backgroundColor = 'rgba(0, 0, 0, 0.05)';
    });
    userMenuButton.addEventListener('mouseleave', function () {
        this.style.backgroundColor = 'transparent';
    });
}

// 初始化所有用户信息相关功能
function initUserInfo() {
    checkUserLogin();
    initUserMenu();
    addUserMenuHoverEffects();
    addUserButtonHoverEffect();
}

// 弹窗函数
function showConfirm(content, title = "Purecpp确认框") {
    return new Promise((resolve) => {
        // 创建弹窗容器
        const modalContainer = document.createElement('div');
        modalContainer.id = 'modal-container';
        modalContainer.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.5);
            display: flex;
            justify-content: center;
            align-items: center;
            z-index: 10000;
        `;

        // 创建弹窗内容
        const modalContent = document.createElement('div');
        modalContent.style.cssText = `
            background-color: #fff;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
            width: 90%;
            max-width: 500px;
            overflow: hidden;
        `;

        // 创建弹窗头部
        const modalHeader = document.createElement('div');
        modalHeader.style.cssText = `
            padding: 16px;
            border-bottom: 1px solid #e0e0e0;
            font-size: 18px;
            font-weight: 600;
            color: #333;
        `;
        modalHeader.textContent = title;

        // 创建弹窗主体
        const modalBody = document.createElement('div');
        modalBody.style.cssText = `
            padding: 20px;
            color: #666;
            line-height: 1.5;
        `;
        modalBody.innerHTML = content;

        // 创建弹窗底部
        const modalFooter = document.createElement('div');
        modalFooter.style.cssText = `
            padding: 16px;
            border-top: 1px solid #e0e0e0;
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        `;

        // 创建取消按钮
        const cancelButton = document.createElement('button');
        cancelButton.textContent = '取消';
        cancelButton.style.cssText = `
            padding: 8px 16px;
            border: 1px solid #ddd;
            border-radius: 4px;
            background-color: #fff;
            color: #333;
            cursor: pointer;
            font-size: 14px;
        `;

        // 创建确认按钮
        const confirmButton = document.createElement('button');
        confirmButton.textContent = '确认';
        confirmButton.style.cssText = `
            padding: 8px 16px;
            border: 1px solid #007bff;
            border-radius: 4px;
            background-color: #007bff;
            color: #fff;
            cursor: pointer;
            font-size: 14px;
        `;

        // 添加按钮到底部
        modalFooter.appendChild(cancelButton);
        modalFooter.appendChild(confirmButton);

        // 组装弹窗
        modalContent.appendChild(modalHeader);
        modalContent.appendChild(modalBody);
        modalContent.appendChild(modalFooter);
        modalContainer.appendChild(modalContent);

        // 添加到页面
        document.body.appendChild(modalContainer);

        // 点击取消按钮
        cancelButton.addEventListener('click', function () {
            document.body.removeChild(modalContainer);
            resolve(false);
        });

        // 点击确认按钮
        confirmButton.addEventListener('click', function () {
            document.body.removeChild(modalContainer);
            resolve(true);
        });

        // 点击遮罩层关闭弹窗
        modalContainer.addEventListener('click', function (e) {
            if (e.target === modalContainer) {
                document.body.removeChild(modalContainer);
                resolve(false);
            }
        });
    });
}

// 消息提示弹窗函数（只有一个确认按钮）
function showAlert(content, title = "Purecpp提示框") {
    return new Promise((resolve) => {
        // 创建弹窗容器
        const modalContainer = document.createElement('div');
        modalContainer.id = 'alert-container';
        modalContainer.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.5);
            display: flex;
            justify-content: center;
            align-items: center;
            z-index: 10000;
        `;

        // 创建弹窗内容
        const modalContent = document.createElement('div');
        modalContent.style.cssText = `
            background-color: #fff;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
            width: 90%;
            max-width: 500px;
            overflow: hidden;
        `;

        // 创建弹窗头部
        const modalHeader = document.createElement('div');
        modalHeader.style.cssText = `
            padding: 16px;
            border-bottom: 1px solid #e0e0e0;
            font-size: 18px;
            font-weight: 600;
            color: #333;
        `;
        modalHeader.textContent = title;

        // 创建弹窗主体
        const modalBody = document.createElement('div');
        modalBody.style.cssText = `
            padding: 20px;
            color: #666;
            line-height: 1.5;
        `;
        modalBody.innerHTML = content;

        // 创建弹窗底部
        const modalFooter = document.createElement('div');
        modalFooter.style.cssText = `
            padding: 16px;
            border-top: 1px solid #e0e0e0;
            display: flex;
            justify-content: center;
            gap: 10px;
        `;

        // 创建确认按钮
        const confirmButton = document.createElement('button');
        confirmButton.textContent = '确认';
        confirmButton.style.cssText = `
            padding: 8px 16px;
            border: 1px solid #007bff;
            border-radius: 4px;
            background-color: #007bff;
            color: #fff;
            cursor: pointer;
            font-size: 14px;
        `;

        // 添加按钮到底部
        modalFooter.appendChild(confirmButton);

        // 组装弹窗
        modalContent.appendChild(modalHeader);
        modalContent.appendChild(modalBody);
        modalContent.appendChild(modalFooter);
        modalContainer.appendChild(modalContent);

        // 添加到页面
        document.body.appendChild(modalContainer);

        // 点击确认按钮
        confirmButton.addEventListener('click', function () {
            document.body.removeChild(modalContainer);
            resolve();
        });

        // 点击遮罩层不关闭弹窗
        // 消息提示需要用户明确点击确认按钮
    });
}
