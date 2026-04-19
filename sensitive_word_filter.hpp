#pragma once
#include <cctype>
#include <fstream>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 基于 Aho-Corasick 算法的敏感词过滤器。
//
// 原理：将所有敏感词构建成一个有限自动机（Trie + failure 链 + dict_suffix 链），
// 过滤时只需扫描文本一遍即可匹配全部词，复杂度 O(文本长度 + 匹配数)，
// 与词库大小无关——10 条和 10 万条词的匹配耗时几乎相同。
//
// 对比旧方案（逐词 string::find）：
//   旧方案  O(词数 × 文本长度)   10000词 × 2000字节 ≈ 2000万次比较
//   新方案  O(文本长度)           2000次状态转移
//
// 匹配大小写不敏感（ASCII），中文按字节匹配（UTF-8 无大小写之分）。
// 线程安全：多线程并发调用 filter()/contains() 使用读锁；load() 使用写锁。

class sensitive_word_filter {
 public:
  static sensitive_word_filter &instance() {
    static sensitive_word_filter inst;
    return inst;
  }

  // 从文件加载词库并重建自动机。'#' 开头和空行忽略。
  void load(const std::string &filepath) {
    std::ifstream f(filepath);
    std::vector<std::string> words;
    std::string line;
    while (std::getline(f, line)) {
      while (!line.empty() &&
             (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      std::string lw = to_lower(line);
      if (!lw.empty()) words.push_back(std::move(lw));
    }
    automaton new_ac = build(words);
    std::unique_lock lk(mtx_);
    ac_ = std::move(new_ac);
  }

  bool empty() const {
    std::shared_lock lk(mtx_);
    return ac_.nodes.size() <= 1;
  }

  // 将文本中所有敏感词替换为等长 '*'，返回过滤后的文本。
  std::string filter(const std::string &text) const {
    std::shared_lock lk(mtx_);
    if (ac_.nodes.size() <= 1) return text;
    return do_filter(text);
  }

  // 检测文本中是否含有任意敏感词。
  bool contains(const std::string &text) const {
    std::shared_lock lk(mtx_);
    if (ac_.nodes.size() <= 1) return false;
    return do_contains(text);
  }

 private:
  // ── Aho-Corasick 节点 ──────────────────────────────────────────────────────
  struct ac_node {
    std::unordered_map<uint8_t, int> ch;
    int fail     = 0;
    int dict_suf = 0;
    int word_len = 0;   // > 0 表示此处是某个词的结尾，值为词的字节长度
    bool ascii_word = false; // 该词是否为纯 ASCII（需要词边界检查）
  };

  struct automaton {
    std::vector<ac_node> nodes;
  };

  // ── 构建自动机 ─────────────────────────────────────────────────────────────
  static automaton build(const std::vector<std::string> &words) {
    automaton a;
    a.nodes.emplace_back();  // 节点 0 = 根

    // 1. 建 Trie
    for (const auto &w : words) {
      int cur = 0;
      for (uint8_t c : w) {
        auto it = a.nodes[cur].ch.find(c);
        if (it == a.nodes[cur].ch.end()) {
          int nxt = static_cast<int>(a.nodes.size());
          a.nodes[cur].ch[c] = nxt;
          a.nodes.emplace_back();
          cur = nxt;
        } else {
          cur = it->second;
        }
      }
      if (a.nodes[cur].word_len == 0) {
        a.nodes[cur].word_len = static_cast<int>(w.size());
        // 纯 ASCII 词（所有字节 < 128）需要词边界检查，避免误杀正常单词
        a.nodes[cur].ascii_word = std::all_of(
            w.begin(), w.end(), [](unsigned char c) { return c < 128; });
      }
    }

    // 2. BFS 建 failure link 和 dict_suffix link
    std::queue<int> q;
    for (auto &[c, child] : a.nodes[0].ch) {
      a.nodes[child].fail = 0;
      q.push(child);
    }

    while (!q.empty()) {
      int u = q.front();
      q.pop();

      // dict_suf(u)：fail(u) 是词尾则指向 fail(u)，否则继承 fail(u).dict_suf
      int f = a.nodes[u].fail;
      a.nodes[u].dict_suf =
          (a.nodes[f].word_len > 0) ? f : a.nodes[f].dict_suf;

      for (auto &[c, v] : a.nodes[u].ch) {
        // fail(v) = 从 fail(u) 开始沿 fail 链找第一个有 c 边的节点
        int p = a.nodes[u].fail;
        while (p != 0 && a.nodes[p].ch.find(c) == a.nodes[p].ch.end())
          p = a.nodes[p].fail;
        auto it = a.nodes[p].ch.find(c);
        a.nodes[v].fail =
            (it != a.nodes[p].ch.end() && it->second != v) ? it->second : 0;
        q.push(v);
      }
    }

    return a;
  }

  // ── 状态转移：从 cur 经字节 c 到达下一个状态 ─────────────────────────────
  static int go(const automaton &a, int cur, uint8_t c) {
    while (cur != 0 && a.nodes[cur].ch.find(c) == a.nodes[cur].ch.end())
      cur = a.nodes[cur].fail;
    auto it = a.nodes[cur].ch.find(c);
    return (it != a.nodes[cur].ch.end()) ? it->second : 0;
  }

  // ── 过滤：扫一遍文本，对匹配区域打 mask，再逐段替换为 '*' ────────────────
  std::string do_filter(const std::string &text) const {
    const std::string lower = to_lower(text);
    std::vector<bool> mask(text.size(), false);

    int cur = 0;
    for (size_t i = 0; i < lower.size(); ++i) {
      cur = go(ac_, cur, static_cast<uint8_t>(lower[i]));

      // 枚举所有在位置 i 结尾的词：先检查当前节点，再沿 dict_suffix 链枚举更短的词
      auto try_mask = [&](int nd) {
        if (ac_.nodes[nd].word_len <= 0) return;
        size_t wl = static_cast<size_t>(ac_.nodes[nd].word_len);
        size_t start = i + 1 - wl;
        // 纯 ASCII 词需要词边界，避免误杀正常单词中的子串（如 "sm" 误杀 "qicosmos"）
        if (ac_.nodes[nd].ascii_word && !has_word_boundary(lower, start, i + 1))
          return;
        for (size_t k = start; k <= i; ++k) mask[k] = true;
      };

      try_mask(cur);
      for (int ds = ac_.nodes[cur].dict_suf; ds != 0; ds = ac_.nodes[ds].dict_suf)
        try_mask(ds);
    }

    // 构建结果：未被 mask 的字节原样保留；被 mask 的连续区域按 Unicode 字符数替换为 '*'
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
      if (!mask[i]) {
        result += text[i++];
      } else {
        size_t j = i;
        while (j < text.size() && mask[j]) ++j;
        result += std::string(codepoint_count(text, i, j), '*');
        i = j;
      }
    }
    return result;
  }

  bool do_contains(const std::string &text) const {
    const std::string lower = to_lower(text);
    int cur = 0;
    for (size_t i = 0; i < lower.size(); ++i) {
      cur = go(ac_, cur, static_cast<uint8_t>(lower[i]));
      auto check = [&](int nd) -> bool {
        if (ac_.nodes[nd].word_len <= 0) return false;
        size_t wl = static_cast<size_t>(ac_.nodes[nd].word_len);
        size_t start = i + 1 - wl;
        if (ac_.nodes[nd].ascii_word && !has_word_boundary(lower, start, i + 1))
          return false;
        return true;
      };
      if (check(cur)) return true;
      for (int ds = ac_.nodes[cur].dict_suf; ds != 0; ds = ac_.nodes[ds].dict_suf)
        if (check(ds)) return true;
    }
    return false;
  }

  // ── 工具函数 ───────────────────────────────────────────────────────────────

  // 判断字节是否为 ASCII 单词字符（字母、数字、下划线）
  static bool is_word_char(unsigned char c) {
    return (c < 128) && (std::isalnum(c) || c == '_');
  }

  // 纯 ASCII 词的词边界检查：匹配区间 [start, end) 前后不能是单词字符
  static bool has_word_boundary(const std::string &text, size_t start, size_t end) {
    if (start > 0 && is_word_char(static_cast<unsigned char>(text[start - 1])))
      return false;
    if (end < text.size() && is_word_char(static_cast<unsigned char>(text[end])))
      return false;
    return true;
  }  static std::string to_lower(const std::string &s) {
    std::string r = s;
    for (auto &c : r)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
  }

  // 统计 s[from, to) 内的 Unicode 码点数（跳过 UTF-8 续字节 10xxxxxx）
  static size_t codepoint_count(const std::string &s, size_t from, size_t to) {
    size_t n = 0;
    for (size_t i = from; i < to; ++i)
      if ((static_cast<unsigned char>(s[i]) & 0xC0u) != 0x80u) ++n;
    return n;
  }

  mutable std::shared_mutex mtx_;
  automaton ac_;
};
