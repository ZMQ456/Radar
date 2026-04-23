# 情绪分析算法详细文档

## 一、概述

本算法基于生理信号（心率、呼吸率、心率变异性、体动数据）进行情绪状态分析，输出主要情绪、次要情绪、情绪强度、效价、唤醒度等多维度指标。

---

## 二、情绪类型定义

| 枚举值 | 情绪类型 | 英文名 |
|--------|----------|--------|
| 0 | 平静 | CALM |
| 1 | 高兴 | HAPPY |
| 2 | 兴奋 | EXCITED |
| 3 | 焦虑 | ANXIOUS |
| 4 | 愤怒 | ANGRY |
| 5 | 悲伤 | SAD |
| 6 | 压力 | STRESSED |
| 7 | 放松 | RELAXED |
| 8 | 未知 | UNKNOWN |

---

## 三、整体算法流程图

```
┌─────────────────────────────────────────────────────────────────┐
│                         开始分析                                  │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    输入数据预处理                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │ 心率数据  │  │ 呼吸数据  │  │  HRV数据  │  │ 体动数据  │      │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘      │
│         │             │             │             │            │
│         └─────────────┴─────────────┴─────────────┘            │
│                           ▼                                      │
│              ┌────────────────────────┐                         │
│              │   滑动窗口平滑处理      │                         │
│              │   (WINDOW_SIZE = 15)   │                         │
│              └────────────────────────┘                         │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    计算各情绪得分                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ CalmScore()    = 0.45×HR + 0.22×Stability + 0.27×RR     │   │
│  │                 + 0.16×RR_reg + 0.10×HRV + 0.08×Move    │   │
│  │                                                              │   │
│  │ HappyScore()   = 0.45×HR + 0.10×Variability + 0.34×RR     │   │
│  │                 + 0.10×HRV + 0.08×Move                    │   │
│  │                                                              │   │
│  │ ExcitedScore() = 0.45×HR + 0.10×Trend + 0.34×RR           │   │
│  │                 + 0.10×HRV + 0.10×Move                     │   │
│  │                                                              │   │
│  │ AnxiousScore() = 0.36×HR + 0.16×Std + 0.25×(1-HRV)        │   │
│  │                 + 0.10×Stress + 0.16×(1-RR_reg)          │   │
│  │                 + 0.10×Move                                │   │
│  │                                                              │   │
│  │ AngryScore()   = 0.36×HR + 0.20×Trend + 0.25×(1-HRV)      │   │
│  │                 + 0.20×(1-RR_reg) + 0.10×Move             │   │
│  │                                                              │   │
│  │ SadScore()     = 0.40×(-HR) + 0.15×(1-Std) + 0.28×(-RR)   │   │
│  │                 + 0.10×RR_reg + 0.10×(1-HRV)              │   │
│  │                 + 0.10×(1-Move)                            │   │
│  │                                                              │   │
│  │ StressedScore()= 0.36×HR + 0.20×Trend + 0.25×(1-HRV)      │   │
│  │                 + 0.10×Stress + 0.16×(1-RR_reg)           │   │
│  │                 + 0.10×Move                                │   │
│  │                                                              │   │
│  │ RelaxedScore() = 0.40×(-HR) + 0.22×(1-Std) + 0.16×(-RR)   │   │
│  │                 + 0.10×RR_reg + 0.10×(1-Move)             │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    概率归一化 #1                                  │
│                                                                 │
│         P_i = Score_i / Σ(Score_j),  j = 0 to 8                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Top1 概率放大                                  │
│                                                                 │
│              Score_max = Score_max × 1.3                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    概率归一化 #2                                  │
│                                                                 │
│         P_i = Score_i / Σ(Score_j),  j = 0 to 8                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    自适应平滑处理                                 │
│                                                                 │
│   diff = |P_i - P_prev_i|                                      │
│   α = (diff > 0.2) ? 0.6 : 0.25                                 │
│   P_i = α × P_i + (1-α) × P_prev_i                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    提取 Top1 和 Top2                              │
│                                                                 │
│   遍历找出: maxProb(最大), secondProb(第二大)                    │
│   primaryEmotion = emotion[maxIdx]                             │
│   secondaryEmotion = emotion[secondIdx]                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    UNKNOWN 判断                                  │
│                                                                 │
│   if (maxProb < 0.20 && (maxProb - secondProb) < 0.03)         │
│       primaryEmotion = UNKNOWN                                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    负面情绪合并                                   │
│                                                                 │
│   if (primaryEmotion ∈ {ANXIOUS, ANGRY, STRESSED})             │
│       combinedProb = P[ANXIOUS] + P[ANGRY] + P[STRESSED]       │
│       primaryEmotion = STRESSED                                │
│       confidence = max(combinedProb, 各负面情绪概率)             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    计算情绪强度                                   │
│                                                                 │
│   hrFactor   = |HR - HR_baseline| / 40.0                       │
│   hrvFactor  = 1 - sigmoid(HRV_rmssd, 0.02, 40)                │
│   rrFactor   = |RR - RR_baseline| / 10.0                       │
│                                                                 │
│   intensity = 0.4 + 0.3×clamp(hrFactor)                        │
│             + 0.2×clamp(hrvFactor) + 0.1×clamp(rrFactor)       │
│                                                                 │
│   intensity = clamp(intensity, 0.3, 1.0)                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    计算情绪维度                                   │
│                                                                 │
│   valence = (P[hAPPY] + P[eXCITED] + P[rELAXED] + P[cALM])    │
│            - (P[aNXIOUS] + P[aNGRY] + P[sAD] + P[sTRESSED])    │
│                                                                 │
│   arousal = (P[eXCITED] + P[aNXIOUS] + P[aNGRY])              │
│            / (P[eXCITED] + P[aNXIOUS] + P[aNGRY]               │
│               + P[cALM] + P[rELAXED] + P[sAD])                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    UNKNOWN 强制落地                               │
│                                                                 │
│   if (primaryEmotion == UNKNOWN)                                │
│       if (arousal > 0.6)      → EXCITED                        │
│       else if (valence < -0.2) → STRESSED                     │
│       else                     → CALM                         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    计算压力水平                                   │
│                                                                 │
│   stressLevel     = f(HRV, HR, RR, 压力指数)                    │
│   anxietyLevel    = f(HR, HRV, 呼吸规律性)                      │
│   relaxationLevel = f(HRV, HR, 体动)                            │
│                                                                 │
│   sympatheticActivity     = 1 - autonomicBalance              │
│   parasympatheticActivity  = autonomicBalance                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                         输出结果                                 │
│   primaryEmotion, secondaryEmotion, confidence, intensity,      │
│   valence, arousal, stressLevel, anxietyLevel,                 │
│   relaxationLevel, sympatheticActivity, parasympatheticActivity │
└─────────────────────────────────────────────────────────────────┘
```

---

## 四、核心计算函数详解

### 4.1 Sigmoid 函数

```cpp
float sigmoid(float x, float k, float x0) {
    return 1.0f / (1.0f + exp(-k * (x - x0)));
}
```

**数学公式：**
```
σ(x; k, x₀) = 1 / (1 + e^(-k(x-x₀)))
```

**参数说明：**
- `k`: 斜率参数，控制曲线的陡峭程度
- `x₀`: 中心点参数，曲线中点对应的x值

**函数图像特征：**
- 当 `x = x₀` 时，σ = 0.5
- 当 `k > 0` 时，曲线单调递增
- `k` 越大，曲线越陡峭

---

### 4.2 Gaussian 函数

```cpp
float gaussian(float x, float mean, float std) {
    float diff = x - mean;
    return exp(-(diff * diff) / (2 * std * std));
}
```

**数学公式：**
```
G(x; μ, σ) = exp(-(x-μ)² / (2σ²))
```

**参数说明：**
- `μ`: 均值，函数峰值位置
- `σ`: 标准差，控制曲线宽度

**函数图像特征：**
- 当 `x = μ` 时，G = 1（最大值）
- `σ` 越小，曲线越尖锐
- `σ` 越大，曲线越平缓

---

### 4.3 归一化函数

```cpp
// 心率归一化
float normalizeHR(float hr, float baseline) {
    return (hr - baseline) / baseline;
}

// 呼吸率归一化
float normalizeRR(float rr, float baseline) {
    return (rr - baseline) / baseline;
}

// HRV归一化
float normalizeHRV(float rmssd) {
    return clamp(rmssd / 100.0f, 0.0f, 1.5f);
}

// 体动归一化
float normalizeMovement(float movement) {
    return clamp(movement / 100.0f, 0.0f, 1.0f);
}
```

---

## 五、各情绪评分算法

### 5.1 平静情绪 (Calm)

**适用场景：** 心率稳定、接近静息基线，呼吸规律，体动较低

**计算公式：**
```
Score_calm = 0.45 × G(|HR_norm|, 0, 0.3)
           + 0.22 × (1 - clamp(HR_std / 12, 0, 1))
           + 0.27 × G(|RR_norm|, 0, 0.5)
           + 0.16 × RR_regularity
           + 0.10 × σ(HRV_norm, 2.0, 0.7)
           + 0.08 × G(Move_norm, 0.15, 0.2)
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.45 | 最重要，心率需接近基线 |
| 心率稳定性 | 0.22 | 心率变化小 |
| 呼吸率偏离度 | 0.27 | 呼吸需正常 |
| 呼吸规律性 | 0.16 | 呼吸规律 |
| HRV水平 | 0.10 | HRV正常或较高 |
| 体动水平 | 0.08 | 体动低 |

---

### 5.2 高兴情绪 (Happy)

**适用场景：** 心率略高于基线、有适度变异性，呼吸正常，HRV较高

**计算公式：**
```
Score_happy = 0.45 × G(HR_norm, 0.3, 0.25)
           + 0.10 × [1.5 < HR_std < 10 ? 1 : 0]
           + 0.34 × G(|RR_norm|, 0, 0.5)
           + 0.10 × σ(HRV_norm, 2.0, 0.9)
           + 0.08 × G(Move_norm, 0.45, 0.25)
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.45 | 略高于基线，Gaussian中心0.3 |
| 心率变异性 | 0.10 | 适度变异(1.5-10) |
| 呼吸率偏离度 | 0.34 | 呼吸正常 |
| HRV水平 | 0.10 | HRV较高 |
| 体动水平 | 0.08 | 适中活跃 |

---

### 5.3 兴奋情绪 (Excited)

**适用场景：** 心率显著升高且趋势上升，呼吸加快，HRV中等，体动较高

**计算公式：**
```
Score_excited = 0.45 × σ(HR_norm, 2.0, 0.6)
             + 0.10 × [HR_trend > 1.5 ? 1 : 0]
             + 0.34 × σ(RR_norm, 2.0, 0.5)
             + 0.10 × G(HRV_norm, 0.7, 0.4)
             + 0.10 × σ(Move_norm, 2.0, 0.6)
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.45 | 显著升高( sigmoid中心0.6) |
| 心率趋势 | 0.10 | 趋势上升加分 |
| 呼吸率偏离度 | 0.34 | 呼吸加快 |
| HRV水平 | 0.10 | 中等HRV |
| 体动水平 | 0.10 | 高活跃 |

---

### 5.4 焦虑情绪 (Anxious)

**适用场景：** 心率升高但变异小，HRV低，压力指数高，呼吸不规律

**计算公式：**
```
Score_anxious = 0.36 × σ(HR_norm, 2.0, 0.4)
              + 0.16 × (1 - σ(HR_std / 6, 2.0, 0.5))
              + 0.25 × (1 - σ(HRV_norm, 2.0, 0.6))
              + 0.10 × σ(HRV.stressIndex / 30, 2.0, 0.5)
              + 0.16 × (1 - RR_regularity)
              + 0.10 × G(Move_norm, 0.55, 0.3)
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.36 | 升高( sigmoid中心0.4，较早触发) |
| 心率稳定性 | 0.16 | 变异小（焦虑时心率僵化）|
| HRV水平 | 0.25 | HRV低（高权重）|
| 压力指数 | 0.10 | 压力指数高 |
| 呼吸规律性 | 0.16 | 呼吸不规律 |
| 体动水平 | 0.10 | 躁动状态 |

---

### 5.5 愤怒情绪 (Angry)

**适用场景：** 心率快速升高且趋势明显，HRV低，呼吸不规律，体动高

**计算公式：**
```
Score_angry = 0.36 × σ(HR_norm, 2.0, 0.75)
            + 0.20 × σ(HR_trend / 4, 2.0, 0.5)
            + 0.25 × (1 - σ(HRV_norm, 2.0, 0.5))
            + 0.20 × (1 - RR_regularity)
            + 0.10 × σ(Move_norm, 2.0, 0.7)
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.36 | 显著升高( sigmoid中心0.75) |
| 心率趋势 | 0.20 | 快速上升趋势 |
| HRV水平 | 0.25 | HRV低 |
| 呼吸规律性 | 0.20 | 不规律 |
| 体动水平 | 0.10 | 高活跃 |

---

### 5.6 悲伤情绪 (Sad)

**适用场景：** 心率偏低，呼吸浅慢，HRV低，体动低

**计算公式：**
```
Score_sad = 0.40 × σ(-HR_norm, 2.0, 0.2)
          + 0.15 × (1 - σ(HR_std / 4, 2.0, 0.3))
          + 0.28 × σ(-RR_norm, 2.0, 0.3)
          + 0.10 × RR_regularity
          + 0.10 × (1 - σ(HRV_norm, 2.0, 0.4))
          + 0.10 × (1 - σ(Move_norm, 2.0, 0.15))
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.40 | 心率偏低（负偏离）|
| 心率稳定性 | 0.15 | 变化小 |
| 呼吸率偏离度 | 0.28 | 呼吸浅慢（负偏离）|
| 呼吸规律性 | 0.10 | 规律 |
| HRV水平 | 0.10 | HRV低 |
| 体动水平 | 0.10 | 低活动 |

---

### 5.7 压力情绪 (Stressed)

**适用场景：** 心率升高，HRV低，压力指数高，呼吸不规律

**计算公式：**
```
Score_stressed = 0.36 × σ(HR_norm, 2.0, 0.5)
              + 0.20 × σ(HR_trend / 3, 2.0, 0.5)
              + 0.25 × (1 - σ(HRV_norm, 2.0, 0.4))
              + 0.10 × σ(HRV.stressIndex / 25, 2.0, 0.5)
              + 0.16 × (1 - RR_regularity)
              + 0.10 × G(Move_norm, 0.6, 0.25)
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.36 | 中度升高 |
| 心率趋势 | 0.20 | 上升趋势 |
| HRV水平 | 0.25 | HRV低（高权重）|
| 压力指数 | 0.10 | 压力指数高 |
| 呼吸规律性 | 0.16 | 不规律 |
| 体动水平 | 0.10 | 适中 |

---

### 5.8 放松情绪 (Relaxed)

**适用场景：** 心率偏低，变异适中，呼吸慢且规律，体动极低

**计算公式：**
```
Score_relaxed = 0.40 × σ(-HR_norm, 2.0, 0.25)
              + 0.22 × (1 - σ(HR_std / 8, 2.0, 0.4))
              + 0.16 × σ(-RR_norm, 2.0, 0.3)
              + 0.10 × RR_regularity
              + 0.10 × (1 - σ(Move_norm, 2.0, 0.15))
```

**权重分析：**
| 指标 | 权重 | 说明 |
|------|------|------|
| 心率偏离度 | 0.40 | 心率偏低（负偏离）|
| 心率稳定性 | 0.22 | 稳定 |
| 呼吸率偏离度 | 0.16 | 呼吸慢（负偏离）|
| 呼吸规律性 | 0.10 | 规律 |
| 体动水平 | 0.10 | 极低活动 |

---

## 六、次要情绪算法

### 6.1 计算原理

次要情绪是概率第二大的情绪类型，不具有独立的评分模型。

```
secondaryEmotion = argmax_i (P_i)，其中 i ≠ primaryEmotion
```

### 6.2 提取算法

```cpp
int secondIdx = 0;
float maxProb = emotionProbs[0], secondProb = 0;
maxIdx = 0;

for (int i = 1; i < 9; i++) {
    if (emotionProbs[i] > maxProb) {
        secondProb = maxProb;
        secondIdx = maxIdx;
        maxProb = emotionProbs[i];
        maxIdx = i;
    } else if (emotionProbs[i] > secondProb) {
        secondProb = emotionProbs[i];
        secondIdx = i;
    }
}

result.primaryEmotion = (EmotionType)maxIdx;
result.secondaryEmotion = (EmotionType)secondIdx;
result.confidence = maxProb;
```

### 6.3 输出格式

```cpp
Serial.printf("主要情绪:%s (置信度: %.1f%%);\n",
    EMOTION_NAMES[emotionResult.primaryEmotion],
    emotionResult.confidence * 100);

Serial.printf("次要情绪: %s倾向\n",
    EMOTION_NAMES[emotionResult.secondaryEmotion]);

Serial.printf("主要情绪得分:%.3f, 次要情绪得分:%.3f, 差值: %.3f\n",
    emotionResult.confidence,
    emotionResult.confidence * 0.8f,  // 估算的次要得分
    emotionResult.confidence * 0.2f);
```

---

## 七、概率处理流程

### 7.1 归一化公式

```cpp
void SimpleEmotionAnalyzer::normalizeProbabilities() {
    float sum = 0;
    for (int i = 0; i < 9; i++) {
        sum += emotionProbs[i];
    }
    if (sum > 0) {
        for (int i = 0; i < 9; i++) {
            emotionProbs[i] /= sum;
        }
    }
}
```

**数学表达：**
```
P_i = Score_i / Σ Score_j,  j ∈ {0,1,2,...,8}
```

### 7.2 Top1放大机制

```cpp
// 找出最大概率索引
int maxIdx = 0;
for (int i = 1; i < 9; i++) {
    if (emotionProbs[i] > emotionProbs[maxIdx]) {
        maxIdx = i;
    }
}

// Top1放大1.3倍
emotionProbs[maxIdx] *= 1.3f;

// 再次归一化
normalizeProbabilities();
```

**目的：** 强行制造"赢家"，增强主要情绪的区分度

### 7.3 自适应平滑

```cpp
void SimpleEmotionAnalyzer::smoothProbabilities() {
    for (int i = 0; i < 9; i++) {
        float diff = fabs(emotionProbs[i] - prevProbs[i]);
        float adaptiveAlpha = (diff > 0.2f) ? 0.6f : 0.25f;

        emotionProbs[i] = adaptiveAlpha * emotionProbs[i] +
                         (1.0f - adaptiveAlpha) * prevProbs[i];
        prevProbs[i] = emotionProbs[i];
    }
}
```

**数学表达：**
```
P_i(t) = α × P_i(t) + (1-α) × P_i(t-1)

其中：
  α = 0.6, 当 |P_i(t) - P_i(t-1)| > 0.2（变化快 → 快响应）
  α = 0.25, 当 |P_i(t) - P_i(t-1)| ≤ 0.2（变化慢 → 适度抑制）
```

---

## 八、情绪强度计算

### 8.1 计算公式

```cpp
float hrFactor = fabs(hrData.bpmSmoothed - baseline.hrResting) / 40.0f;
float hrvFactor = hrvData.isValid ? (1.0f - sigmoid(hrvData.rmssd, 0.02f, 40)) : 0.5f;
float rrFactor = rrData.isValid ? fabs(rrData.rateSmoothed - baseline.rrResting) / 10.0f : 0.3f;

result.intensity = 0.4f
                 + 0.3f * constrain_value(hrFactor, 0.0f, 1.0f)
                 + 0.2f * constrain_value(hrvFactor, 0.0f, 1.0f)
                 + 0.1f * constrain_value(rrFactor, 0.0f, 1.0f);

result.intensity = constrain_value(result.intensity, 0.3f, 1.0f);
```

### 8.2 公式表达

```
intensity = clamp(0.4 + 0.3×clamp(hrFactor) + 0.2×clamp(hrvFactor) + 0.1×clamp(rrFactor), 0.3, 1.0)

其中：
  hrFactor  = |HR - HR_baseline| / 40
  hrvFactor = 1 - σ(HRV_rmssd, 0.02, 40)
  rrFactor  = |RR - RR_baseline| / 10
```

### 8.3 权重分析

| 因素 | 权重 | 说明 |
|------|------|------|
| 基线偏移 | 0.4 | 基础强度 |
| 心率偏移 | 0.3 | 心率偏离基线程度 |
| HRV偏离 | 0.2 | HRV偏离正常程度 |
| 呼吸偏移 | 0.1 | 呼吸偏离程度 |

---

## 九、情绪维度计算

### 9.1 效价 (Valence)

**定义：** 情绪的正面/负面倾向

```cpp
void SimpleEmotionAnalyzer::calculateDimensions(const HeartRateData& hr,
                                                const RespirationData& rr) {
    float positive = emotionProbs[EMOTION_HAPPY] + emotionProbs[EMOTION_EXCITED] +
                    emotionProbs[EMOTION_RELAXED] + emotionProbs[EMOTION_CALM];
    float negative = emotionProbs[EMOTION_ANXIOUS] + emotionProbs[EMOTION_ANGRY] +
                    emotionProbs[EMOTION_SAD] + emotionProbs[EMOTION_STRESSED];

    lastResult.valence = (positive - negative);
}
```

**公式：**
```
valence = (P_happy + P_excited + P_relaxed + P_calm)
        - (P_anxious + P_angry + P_sad + P_stressed)

范围：[-1, +1]
  -1 = 完全负面情绪
   0 = 中性
  +1 = 完全正面情绪
```

### 9.2 唤醒度 (Arousal)

**定义：** 情绪的激活程度

```cpp
    float highArousal = emotionProbs[EMOTION_EXCITED] + emotionProbs[EMOTION_ANXIOUS] +
                       emotionProbs[EMOTION_ANGRY];
    float lowArousal = emotionProbs[EMOTION_CALM] + emotionProbs[EMOTION_RELAXED] +
                      emotionProbs[EMOTION_SAD];
    float total = highArousal + lowArousal;

    lastResult.arousal = total > 0 ? highArousal / total : 0.5f;
```

**公式：**
```
arousal = (P_excited + P_anxious + P_angry)
        / (P_excited + P_anxious + P_angry + P_calm + P_relaxed + P_sad)

范围：[0, 1]
  0 = 低唤醒（平静、放松、悲伤）
  1 = 高唤醒（兴奋、焦虑、愤怒）
```

---

## 十、压力评估计算

### 10.1 压力水平 (Stress Level)

```cpp
void SimpleEmotionAnalyzer::calculateStressLevels(const HeartRateData& hr,
                                                   const RespirationData& rr,
                                                   const HRVEstimate& hrv) {
    float stressScore = 0;

    // HRV压力指数
    if (hrv.isValid) {
        stressScore += 0.4f * sigmoid(hrv.stressIndex / 30.0f, 2.0f, 0.5f);
    }

    // 心率偏离
    if (hr.isValid) {
        float hrNorm = fabs(normalizeHR(hr.bpmSmoothed, baseline.hrResting));
        stressScore += 0.35f * sigmoid(hrNorm, 2.0f, 0.5f);
    }

    // 呼吸不规律
    if (rr.isValid) {
        float irregularity = 1.0f - rr.regularity;
        stressScore += 0.25f * irregularity;
    }

    result.stressLevel = constrain_value(stressScore * 100.0f, 0.0f, 100.0f);
}
```

### 10.2 焦虑水平 (Anxiety Level)

```cpp
    float anxietyScore = 0;

    if (hr.isValid) {
        float hrNorm = normalizeHR(hr.bpmSmoothed, baseline.hrResting);
        anxietyScore += 0.4f * sigmoid(hrNorm, 2.0f, 0.4f);
        anxietyScore += 0.2f * (1.0f - sigmoid(hr.bpmStd / 6.0f, 2.0f, 0.5f));
    }

    if (hrv.isValid) {
        anxietyScore += 0.25f * (1.0f - sigmoid(hrv.rmssd / 50.0f, 2.0f, 0.5f));
    }

    if (rr.isValid) {
        anxietyScore += 0.15f * (1.0f - rr.regularity);
    }

    result.anxietyLevel = constrain_value(anxietyScore * 100.0f, 0.0f, 100.0f);
```

### 10.3 放松水平 (Relaxation Level)

```cpp
    float relaxationScore = 0;

    if (hrv.isValid) {
        relaxationScore += 0.45f * sigmoid(hrv.rmssd / 80.0f, 2.0f, 0.6f);
    }

    if (hr.isValid) {
        float hrNorm = -normalizeHR(hr.bpmSmoothed, baseline.hrResting);
        relaxationScore += 0.30f * sigmoid(hrNorm, 2.0f, 0.25f);
        relaxationScore += 0.15f * (1.0f - sigmoid(hr.bpmStd / 10.0f, 2.0f, 0.5f));
    }

    if (movement.isValid) {
        relaxationScore += 0.10f * (1.0f - sigmoid(movement.movementSmoothed / 30.0f,
                                                      2.0f, 0.5f));
    }

    result.relaxationLevel = constrain_value(relaxationScore * 100.0f, 0.0f, 100.0f);
```

---

## 十一、特殊处理规则

### 11.1 UNKNOWN 判断条件

```cpp
if (maxProb < 0.20f && (maxProb - secondProb) < 0.03f) {
    result.primaryEmotion = EMOTION_UNKNOWN;
    result.confidence = maxProb;
}
```

**触发条件（同时满足）：**
1. 最大概率 < 0.20（低置信度）
2. 最大概率与第二大概率差值 < 0.03（多情绪接近）

### 11.2 负面情绪合并

```cpp
if (result.primaryEmotion == EMOTION_ANXIOUS ||
    result.primaryEmotion == EMOTION_ANGRY ||
    result.primaryEmotion == EMOTION_STRESSED) {

    float combinedProb = emotionProbs[EMOTION_ANXIOUS] +
                        emotionProbs[EMOTION_ANGRY] +
                        emotionProbs[EMOTION_STRESSED];
    result.primaryEmotion = EMOTION_STRESSED;
    result.confidence = max(combinedProb,
                          max(emotionProbs[EMOTION_ANXIOUS],
                              max(emotionProbs[EMOTION_ANGRY],
                                  emotionProbs[EMOTION_STRESSED])));
}
```

**逻辑：** 焦虑、愤怒、压力三种情绪合并为"压力"情绪

### 11.3 UNKNOWN 强制落地

```cpp
if (result.primaryEmotion == EMOTION_UNKNOWN) {
    if (result.arousal > 0.6f) {
        result.primaryEmotion = EMOTION_EXCITED;
    } else if (result.valence < -0.2f) {
        result.primaryEmotion = EMOTION_STRESSED;
    } else {
        result.primaryEmotion = EMOTION_CALM;
    }
}
```

**规则：**
| 条件 | 映射结果 |
|------|----------|
| arousal > 0.6 | EXCITED（高唤醒→兴奋）|
| valence < -0.2 且 arousal ≤ 0.6 | STRESSED（负面→压力）|
| 其他 | CALM（默认平静）|

---

## 十二、输出数据结构

```cpp
struct EmotionResult {
    EmotionType primaryEmotion;      // 主要情绪
    EmotionType secondaryEmotion;     // 次要情绪
    float confidence;                 // 置信度 0-1
    float intensity;                  // 强度 0-1

    float valence;                    // 效价 -1到+1
    float arousal;                    // 唤醒度 0-1

    float stressLevel;               // 压力水平 0-100
    float anxietyLevel;               // 焦虑水平 0-100
    float relaxationLevel;           // 放松水平 0-100

    float sympatheticActivity;       // 交感神经活动
    float parasympatheticActivity;   // 副交感神经活动

    bool isValid;                     // 数据有效性
    unsigned long timestamp;          // 时间戳
};
```

---

## 十三、算法特点总结

| 特点 | 说明 |
|------|------|
| 多指标融合 | 综合心率、呼吸、HRV、体动4类数据 |
| 权重分配 | 不同情绪对各指标的权重不同 |
| 非线性函数 | 使用Sigmoid和Gaussian模拟生理反应 |
| 自适应平滑 | 根据变化速度动态调整平滑系数 |
| Top1放大 | 增强主要情绪区分度 |
| 负面情绪合并 | 焦虑/愤怒/压力统一为压力评估 |
| UNKNOWN处理 | 低置信度时标记为未知并强制落地 |
