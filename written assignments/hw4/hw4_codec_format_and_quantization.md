# HW4 代码解释: 编码格式、量化与 `.dic53` 说明

由于Homework4 是小波变换的作业，直接实现从原图到JPEG和从JPEG读取过于复杂，故wrap 了 Opencv的图像读取/写入相关工具函数

## 1. 总体流程

1. 使用 OpenCV 读取 `8-bit` 的单通道或三通道图像。
2. 对每个通道分别执行整数可逆的 5/3 lifting 小波变换。
3. 对每一层分解中的高频子带 `LH / HL / HH` 做有损量化，`LL` 子带不量化。
4. 将量化后的系数组织成内部系数平面。
5. 做简单零游程编码，对生成的字节流做 Huffman 熵编码，最后写入 `.dic53` 文件（其实应该写成jpeg，但是太复杂了）。
6. 同时根据量化后的系数生成一张 preview 图，仅用于展示变换域分布，不参与解码。
7. 解码时读取 `.dic53`，先做 Huffman 解码，再做零游程展开，恢复量化系数；之后反量化、反小波变换，并裁剪到 `[0, 255]` 输出图像。

整个流程中：

- 5/3 变换本身仍然是整数可逆变换。
- 主流程因为加入了量化，所以整体是有损压缩。
- `.dic53` 现在已经不是“裸系数转储”，而是“量化系数 + 简单熵编码”的容器。

系数压缩：

- 第一级：对量化系数做简单零游程编码，把长串 `0` 变成 `(zero-run, run_length)` token。
- 第二级：对 token 字节流使用 Huffman 编码，进一步压缩常见字节模式。

- `hw1`：使用其字节频数分析能力统计 token 流的 256 个符号频数。
- `hw2`：使用其 canonical Huffman 实现对 token 字节流编码和解码。

## 3. `.dic53` 文件格式

### 3.1 文件头

| 字段名 | 类型 | 含义 |
| --- | --- | --- |
| `magic` | 5 bytes | 固定 ASCII `"DIC53"` |
| `version` | `uint32_t` | 文件格式版本，当前为 `2` |
| `width` | `uint32_t` | 原图宽度 |
| `height` | `uint32_t` | 原图高度 |
| `channels` | `uint32_t` | 通道数，仅允许 `1` 或 `3` |
| `levels` | `uint32_t` | 小波分解层数 |
| `quality` | `uint32_t` | 质量参数，范围 `1..100` |
| `flags` | `uint32_t` | 预留字段，当前为 `0` |

### 3.2 头之后的数据

1. `256` 个 `uint32_t` 频数项
2. 一个 Huffman bitstream

具体来说：

- 先把量化后的系数转成 token 字节流。
- 再统计该 token 字节流每个字节值 `0..255` 的出现次数。
- 这 256 个频数被写入文件，作为重建 Huffman 树的依据。
- 最后写入 Huffman 编码后的 bitstream。

解码时：

1. 读取 256 项频数表。
2. 用与 `hw2` 相同的 canonical Huffman 构树逻辑重建码表。
3. 读取 bitstream 并解码回 token 字节流。
4. 再把 token 流展开回量化系数数组。

## 4. 系数组织方式

### 4.1 通道顺序

如果输入是三通道图像，内部和文件中的通道顺序都沿用 OpenCV 默认的 `BGR` 顺序。

### 4.2 每通道系数平面

每个通道内部对应一个与原图尺寸相同的 `int32_t` 系数平面。子带布局采用原地排布：

- 左上：`LL`
- 右上：`HL`
- 左下：`LH`
- 右下：`HH`

多层分解时，只对上一层的 `LL` 子带继续递归分解，因此左上角会继续嵌套更深层的 `LL / HL / LH / HH` 结构。


## 5. 量化规则

### 5.1 量化对象

在每一层分解中：

- `LL` 子带不量化
- `LH / HL / HH` 三个高频子带共享同一量化步长

### 5.2 `quality` 参数含义

命令行中的 `quality` 范围为 `1..100`：

- `quality = 100`：量化最弱，失真最小
- `quality = 1`：量化最强，失真最大

内部先计算基础步长：

```c
base_step = 1 + ((100 - quality) * 31) / 99;
```

因此：

- `quality = 100` 时，`base_step = 1`
- `quality = 1` 时，`base_step = 32`

### 5.3 分层量化步长

设总分解层数为 `levels`，当前层编号为 `current_level`，其中：

- `current_level = 1` 表示最浅层
- `current_level = levels` 表示最深层

则当前层的步长为：

```c
layer_step = base_step * (levels - current_level + 1);
```


### 5.4 量化与反量化

编码时：

```text
quantized = round(coeff / layer_step)
```

解码时：

```text
dequantized = quantized * layer_step
```

由于量化本身不可逆，所以：

- 5/3 变换本身可逆
- 主流程整体不可逆，是有损压缩

## 6. 简单熵编码设计

量化系数首先被转成 token 字节流。当前只使用两类 token：

- `zero-run`：表示一段连续零系数
- `literal`：表示一个非零整数系数

其中：

- 连续零段长度使用 varuint 编码
- 非零系数先做 zigzag 编码，再做 varuint 编码

