# C++11 基于范围的 for 循环 (Range-based for loop) 复盘

掌握基于范围的 for 循环，是从“C 语言思维”跨越到“现代 C++ 思维”的重要一步。

## 1. 核心概念：它解决了什么问题？

在 C++11 之前，如果你想遍历一个数组或者容器（比如 `std::vector`，`std::list`，`std::map` 等），通常有两种痛苦的写法：

### 痛苦一：基于索引（C 语言老油条风格）
```cpp
std::vector<int> nums = {1, 2, 3, 4, 5};
for (size_t i = 0; i < nums.size(); ++i) {
    int val = nums[i];
    // 还要小心 i 不要越界
}
```

### 痛苦二：基于迭代器（C++98 学院派风格）
```cpp
std::vector<int> nums = {1, 2, 3, 4, 5};
for (std::vector<int>::const_iterator it = nums.begin(); it != nums.end(); ++it) {
    int val = *it;
    // 代码又臭又长，看着辣眼睛
}
```

### C++11 救星降临：基于范围的 for 循环
它直接抛弃了“索引”和“迭代器”这些底层细节，让你专注于业务逻辑：“把容器里的东西一个个拿出来用就行了”。
```cpp
std::vector<int> nums = {1, 2, 3, 4, 5};
for (int val : nums) {
    // 优雅、干净、直观
}
```

---

## 2. 语法公式

它的基本骨架非常简单：
```cpp
for ( 元素声明 : 遍历的对象容器 ) {
    // 循环体
}
```
*   **遍历的对象容器**：可以是数组（如 `int arr[5]`）、标准库容器（如 `vector`, `string`, `map`）、或者任何实现了 `begin()` 和 `end()` 方法的自定义对象。
*   **元素声明**：定义一个变量，用来在每次循环中接收容器里提取出来的那个元素。强烈建议配合 `auto` 关键字使用。

---

## 3. 三大实战心法（最常用的三种写法）

根据你对数据的操作需求（只读、修改、拷贝），我们有三种标准写法。这也是面试和日常开发中最容易拉开差距的地方。

### 心法一：只读不改，追求极速 —— `const auto&` （最常用、最推荐）
例如：`for (const auto& network : networks)`
*   **用法**：当你只想**读取**数据，不需要修改，且数据比较复杂（比如结构体、类对象、长字符串）时使用。
*   **`&` 的作用**：防止拷贝，直接引用原对象，速度极快。
*   **`const` 的作用**：加锁，防止你在循环里手滑改了数据。
*   **示例**：
```cpp
std::vector<std::string> names = {"Alice", "Bob", "Charlie"};
for (const auto& name : names) {
    std::cout << name << std::endl; // 只读打印，完美
}
```

### 心法二：我要修改原始数据 —— `auto&`
*   **用法**：当你需要遍历容器，并且**修改容器里面的原始数据**时使用。
*   **`&` 的作用**：拿到原始数据的引用（遥控器），你在循环里改了变量，容器里的数据就跟着变了。
*   **示例**：
```cpp
std::vector<int> prices = {10, 20, 30};
// 给每个价格涨价 5 块钱
for (auto& price : prices) {
    price += 5; // 原数组变成了 {15, 25, 35}
}
```

### 心法三：轻量级数据，随便折腾 —— `auto`
*   **用法**：当数据非常小（比如基础类型 `int`, `char`, `bool`），拷贝的代价微乎其微，或者你故意想在循环里对拷贝的值进行折腾（不影响原数据）时使用。
*   **特点**：每次循环都会触发一次复制。
*   **示例**：
```cpp
std::vector<int> scores = {90, 80, 70};
for (auto score : scores) {
    score += 10; // 这里只改了拷贝的值，原数组 scores 依然是 {90, 80, 70}
}
```

---

## 4. 高阶技巧：如何遍历 `std::map` (字典/哈希表)

遍历键值对（Key-Value）是这个语法大放异彩的地方，搭配 C++17 的结构化绑定，简直爽到飞起：

**C++11 的写法：**
```cpp
std::map<std::string, int> ages = {{"Tom", 18}, {"Jerry", 16}};
for (const auto& pair : ages) {
    std::cout << pair.first << " is " << pair.second << " years old.\n";
}
```

**C++17 的终极优雅写法（结构化绑定）：**
```cpp
std::map<std::string, int> ages = {{"Tom", 18}, {"Jerry", 16}};
for (const auto& [name, age] : ages) {
    std::cout << name << " is " << age << " years old.\n";
}
```

---

## 5. 总结与复盘

*   **什么时候用？** 只要你需要**从头到尾**遍历一个容器，无脑首选范围 for 循环。
*   **什么时候不用？**
    1. 你需要同时遍历两个数组。
    2. 你需要从中间某个位置开始遍历，或者只遍历一半。
    3. 你在循环过程中需要知道当前元素对应的**索引位置（Index）**（这种情况下老老实实用传统 `for (size_t i = 0...`）。
*   **核心法则口诀**：
    *   **读大对象防拷贝** ➡️ `const auto&`
    *   **修改数据要引用** ➡️ `auto&`
    *   **基础类型随便造** ➡️ `auto`
