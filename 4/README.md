# 任务 4：中间代码优化

本次任务要求你亲自动手实现一些 LLVM IR 上的优化算法，以提高代码的性能。

## 目录作用

本目录是一个基于 C++17 和 LLVM PassManager 的中间代码优化器。程序读取 LLVM IR，按预设顺序运行一组自定义 AnalysisPass 和 TransformPass，最后输出语义等价、尽可能更高效的 LLVM IR。

项目同时提供两个构建目标：

- `task4-classic`：使用固定顺序运行传统 LLVM 优化 Pass。
- `task4-llm`：在传统 Pass 基础上接入 Python/OpenAI 兼容接口，由大模型根据 IR 和 Pass 摘要预测优化顺序。

## 输入

- 启用复活

  任务 3 的标准答案输出，即 clang 在 O0 优化级别下生成的 LLVM IR 文件。

- 禁用复活

  任务 0 的标准答案输出，即 clang 预处理后的测例源代码文件。

## 输出

文本格式的 LLVM IR（`.ll` 文件），其语义必须与输入相同，但运行性能越高越好。

## 构建

该目录的 `CMakeLists.txt` 会被上层工程引用，并生成以下目标：

```bash
cmake --build build --target task4
```

也可以分别构建：

```bash
cmake --build build --target task4-classic
cmake --build build --target task4-llm
```

`task4-llm` 依赖 `pybind11::embed` 以及 Python 侧的 `openai` 包；如果只需要本地固定顺序优化，使用 `task4-classic` 即可。

## 运行

优化器接受两个参数：输入 IR 路径和输出 IR 路径。

```bash
./task4-classic input.ll output.ll
```

程序会：

1. 读取并解析输入 LLVM IR；
2. 初始化 LLVM AnalysisManager 和 ModulePassManager；
3. 注册并运行自定义优化 Pass；
4. 输出优化后的 LLVM IR；
5. 使用 `llvm::verifyModule` 校验输出模块。

## 评测

评测后，每个测例目录下会生成以下文件：

- `score.txt`

  评测的得分详情。

- `anwser.ll`

  标准答案，即 `clang -S -emit-llvm` 的输出。

- `output.ll`

  你的答案（如果 task3 程序正常运行）。

- `anwser.exe`

  标准答案 `answer.ll` 进一步编译出的可执行文件。

- `output.exe`

  你的 `output.ll` 进一步编译出的可执行文件（如果成功）。

- `answer.compile`

  clang 在编译 `answer.ll` 过程中的输出。

- `output.compile`

  clang 在编译 `output.ll` 过程中的输出。

- `answer.out` 和 `answer.err`

  运行 `answer.exe` 时的标准输出（cout）和标准错误输出（cerr）。

- `output.out` 和 `output.err`

  运行 `output.exe` 时的标准输出（cout）和标准错误输出（cerr）。

## 基础代码

基础代码演示了 LLVM AnalysisPass 和 TransformPass 的实现以及使用，包括：

- `StaticCallCounter`

  这是一个 AnalysisPass，用于统计每个函数在模块中被调用的次数。

- `StaticCallCounterPrinter`

  这是一个 AnalysisPass，用于打印前述 StaticCallCounter 的结果。

  > 结果被打印到标准错误输出（`llvm::errs()`），CTest 默认会将输出保存到构建目录（`build`）下的 `Testing` 目录中，运行测试后到该目录下寻找后缀为 `.log` 的日志文件查看输出。

- `ConstantFolding`

  这是一个 TransformPass，用于将操作数全部为常数的指令直接替换为结果，以避免在输出程序中重复计算。

## 已实现的优化 Pass

- `Mem2Reg`

  将可提升的栈上变量转换为 SSA 寄存器形式，减少不必要的 `alloca`、`load`、`store`。

- `ConstantPropagation`

  对未被修改且非数组的全局常量进行传播，用常量替换对应 `load`。

- `ConstantFolding`

  折叠常量二元运算和常量整数比较，并删除被替换的指令。

- `CommonSubexpressionElimination`

  在基本块内复用已经计算过的相同二元表达式，删除重复计算。

- `DeadCodeElimination`

  删除没有有效用户的二元运算，以及写入未被读取全局变量的 `store`。

- `InstructionCombining`

  合并连续加法中的常量项，例如将 `(x + c1) + c2` 合并为 `x + (c1 + c2)`。

- `IfCombine`

  将常量条件分支改写为无条件跳转，并合并只有唯一前驱的基本块。

- `ExtractLoopVariable`

  对循环不变量进行外提，把可安全移动的循环内指令移动到循环 preheader。

- `FunctionInlining`

  内联非递归的小函数调用，并删除不再使用的内部函数。

- `StrengthReduction`

  对乘以、除以或取模 2 的幂进行强度削弱，例如改写为移位或按位与。

## LLM 辅助模式

`llm/` 目录包含 LLM 辅助选择 Pass 顺序所需的 C++ 封装、Python 帮助函数和提示词模板：

- `LLMHelper.*`：通过 `pybind11` 调用 Python 实现的 OpenAI 兼容客户端。
- `PassSequencePredict.*`：生成 Pass 摘要，将 IR 和可用 Pass 交给模型，并根据返回序列动态组装 `ModulePassManager`。
- `prompts/*.xml`：用于总结 Pass 和预测 Pass 顺序的提示词模板。

使用该模式前，需要在 `main.cpp` 中为 `PassSequencePredict` 配置可用的 API Key 和 Base URL。当前代码中的 `<api_key>` 和 `<base_url>` 是占位符。
