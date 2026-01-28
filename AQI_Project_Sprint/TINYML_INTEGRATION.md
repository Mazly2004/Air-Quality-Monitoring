# TinyML Integration for Air Quality Monitoring

## Overview
This document outlines the strategy for integrating TinyML (Tiny Machine Learning) models into the Air Quality Monitoring system for real-time inference, forecasting, and predictive analytics on ESP32 devices.

## Table of Contents
1. [Why TinyML for Air Quality?](#why-tinyml)
2. [Model Candidates](#model-candidates)
3. [Data Requirements](#data-requirements)
4. [Training Pipeline](#training-pipeline)
5. [Deployment Strategy](#deployment-strategy)
6. [Implementation Roadmap](#implementation-roadmap)

---

## Why TinyML?

### Benefits
- **Edge Inference**: Run predictions directly on ESP32 without cloud dependency
- **Low Latency**: Real-time anomaly detection and forecasting
- **Privacy**: Sensitive air quality data stays on-device
- **Bandwidth Savings**: Only send alerts/anomalies instead of raw data
- **Offline Operation**: Continue working during network outages

### Use Cases for This Project
1. **Anomaly Detection**: Identify unusual sensor readings (sensor malfunction or actual air quality events)
2. **Short-term Forecasting**: Predict air quality 15-60 minutes ahead
3. **Classification**: Categorize air quality levels (Good/Moderate/Unhealthy/Hazardous)
4. **Sensor Fusion**: Combine multiple sensor inputs for more accurate readings
5. **Predictive Maintenance**: Detect sensor degradation over time

---

## Model Candidates

### 1. **Anomaly Detection Models**

#### a) Autoencoder (Recommended)
- **Framework**: TensorFlow Lite for Microcontrollers
- **Architecture**: Simple feedforward autoencoder
- **Input**: 6 features (PM2.5, PM10, CO2, TVOC, Temp, Humidity)
- **Output**: Reconstruction error (threshold for anomaly)
- **Model Size**: ~10-20KB
- **Inference Time**: ~5-10ms on ESP32

```
Input Layer (6) → Dense(12, relu) → Dense(6, relu) → Dense(12, relu) → Output(6)
```

**Why this?**: Learns normal patterns, flags deviations without labeled anomalies.

#### b) Isolation Forest (Alternative)
- **Framework**: Convert to TFLite via sklearn
- **Pros**: No assumption of data distribution
- **Cons**: Larger model size (~50KB+)

---

### 2. **Forecasting Models**

#### a) LSTM (Long Short-Term Memory) - Recommended
- **Framework**: TensorFlow Lite for Microcontrollers
- **Architecture**: Single LSTM layer with dense output
- **Input**: Time series window (e.g., last 10 readings)
- **Output**: Next 1-4 time step predictions
- **Model Size**: ~30-50KB
- **Inference Time**: ~20-40ms on ESP32

```
Input(10, 6) → LSTM(16) → Dense(6) → Output(1, 6)
```

**Features**: All 6 sensor readings over time window
**Target**: Same 6 features at t+1

#### b) Temporal Convolutional Network (TCN) - Alternative
- **Pros**: Faster inference, parallelizable
- **Cons**: Slightly larger model size

---

### 3. **Classification Models**

#### a) Simple Neural Network (Recommended)
- **Framework**: TensorFlow Lite for Microcontrollers
- **Architecture**: 2-3 layer feedforward network
- **Input**: Current sensor readings (6 features)
- **Output**: Air quality class (0=Good, 1=Moderate, 2=Unhealthy, 3=Hazardous)
- **Model Size**: ~5-10KB
- **Inference Time**: ~3-5ms on ESP32

```
Input(6) → Dense(16, relu) → Dropout(0.3) → Dense(8, relu) → Dense(4, softmax)
```

#### b) Decision Tree Ensemble (Alternative)
- **Framework**: m2cgen or emlearn
- **Pros**: Very fast inference, interpretable
- **Cons**: May need pruning for memory constraints

---

## Data Requirements

### Data Collection Phase

#### Minimum Dataset Requirements
- **Duration**: 2-4 weeks of continuous monitoring
- **Samples**: 50,000-100,000 measurements minimum
- **Frequency**: Current rate (every 10-30 seconds)
- **Diversity**: Include various conditions (day/night, weekday/weekend, different weather)

#### Features to Collect

```python
# Primary Features (from sensors)
features = {
    'pm25': float,      # PM2.5 (µg/m³)
    'pm10': float,      # PM10 (µg/m³)
    'co2': float,       # CO2 (ppm)
    'tvoc': float,      # TVOC (ppb)
    'temp': float,      # Temperature (°C)
    'humidity': float,  # Relative Humidity (%)
}

# Derived Features (to engineer)
derived_features = {
    'pm25_rolling_mean_10': float,     # 10-point moving average
    'pm25_rolling_std_10': float,      # 10-point rolling std dev
    'co2_rate_of_change': float,       # First derivative
    'time_of_day': int,                # Hour (0-23)
    'day_of_week': int,                # Day (0-6)
    'season': int,                     # Season (0-3)
}

# Temporal Features (for LSTM)
sequence_length = 10  # Last 10 readings (e.g., 5 minutes of data at 30s intervals)
```

#### Data Quality Checks
1. **Missing Values**: Handle sensor dropouts/errors
2. **Outliers**: Cap extreme values (e.g., PM2.5 > 500 µg/m³)
3. **Sensor Calibration**: Cross-reference with known good readings
4. **Temporal Continuity**: Ensure no large time gaps

---

### Labels for Supervised Learning

#### 1. Anomaly Detection Labels (Optional, for validation)
```python
# Label anomalies manually or using rules
anomaly_rules = {
    'sudden_spike': 'pm25_rate_of_change > 10',
    'sensor_zero': 'pm25 == 0 and pm10 == 0',
    'impossible_values': 'humidity > 100 or temp < -40',
    'cross_sensor': 'pm25 > 100 and co2 < 400',  # Unlikely combination
}
```

#### 2. Forecast Labels
```python
# Target is future value
X = readings[t-10:t]  # Input: last 10 readings
y = readings[t+1]      # Target: next reading (or t+1 to t+4 for multi-step)
```

#### 3. Classification Labels
```python
# Based on EPA AQI standards
def label_aqi(pm25, pm10, co2):
    if pm25 <= 12 and co2 <= 800:
        return 0  # Good
    elif pm25 <= 35.4 and co2 <= 1200:
        return 1  # Moderate
    elif pm25 <= 55.4 and co2 <= 2000:
        return 2  # Unhealthy for Sensitive Groups
    else:
        return 3  # Unhealthy/Hazardous
```

---

### Data Storage Structure

```
data/
├── raw/
│   ├── device_001_2024_01.csv
│   ├── device_001_2024_02.csv
│   └── device_002_2024_01.csv
├── processed/
│   ├── train.csv           # 70% of data
│   ├── validation.csv      # 15% of data
│   └── test.csv            # 15% of data
└── metadata.json           # Stats, normalization params, etc.
```

#### CSV Format
```csv
timestamp,device_id,pm25,pm10,co2,tvoc,temp,humidity,label
1704067200,device_001,12.3,18.5,450,120,22.5,45.2,0
1704067230,device_001,12.5,18.7,455,122,22.5,45.1,0
...
```

---

## Training Pipeline

### Phase 1: Data Preparation

#### Step 1: Export Data from Backend
```python
# export_training_data.py
import requests
import pandas as pd
from datetime import datetime, timedelta

backend_url = "http://127.0.0.1:8000"
devices = ["device_001", "device_002"]  # Your device IDs

def export_data(start_date, end_date):
    all_data = []
    for device_id in devices:
        response = requests.get(
            f"{backend_url}/api/measurements",
            params={
                "device_id": device_id,
                "start": start_date.isoformat(),
                "end": end_date.isoformat()
            }
        )
        data = response.json()
        all_data.extend(data)
    
    df = pd.DataFrame(all_data)
    df.to_csv(f"data/raw/export_{start_date.date()}_to_{end_date.date()}.csv", index=False)
    return df

# Export last 30 days
end_date = datetime.now()
start_date = end_date - timedelta(days=30)
df = export_data(start_date, end_date)
print(f"Exported {len(df)} measurements")
```

#### Step 2: Feature Engineering
```python
# preprocess.py
import pandas as pd
import numpy as np

def engineer_features(df):
    df = df.sort_values('timestamp')
    
    # Rolling statistics (10-point window ≈ 5 minutes)
    df['pm25_ma10'] = df['pm25'].rolling(window=10, min_periods=1).mean()
    df['pm25_std10'] = df['pm25'].rolling(window=10, min_periods=1).std()
    df['co2_ma10'] = df['co2'].rolling(window=10, min_periods=1).mean()
    
    # Rate of change
    df['pm25_diff'] = df['pm25'].diff()
    df['co2_diff'] = df['co2'].diff()
    
    # Temporal features
    df['timestamp'] = pd.to_datetime(df['timestamp'])
    df['hour'] = df['timestamp'].dt.hour
    df['day_of_week'] = df['timestamp'].dt.dayofweek
    df['is_weekend'] = (df['day_of_week'] >= 5).astype(int)
    
    # Cyclical encoding for time
    df['hour_sin'] = np.sin(2 * np.pi * df['hour'] / 24)
    df['hour_cos'] = np.cos(2 * np.pi * df['hour'] / 24)
    
    # Labels for classification
    df['aqi_class'] = df.apply(lambda x: classify_aqi(x['pm25'], x['co2']), axis=1)
    
    return df

def classify_aqi(pm25, co2):
    if pm25 <= 12 and co2 <= 800:
        return 0
    elif pm25 <= 35.4 and co2 <= 1200:
        return 1
    elif pm25 <= 55.4 and co2 <= 2000:
        return 2
    else:
        return 3
```

#### Step 3: Normalization
```python
from sklearn.preprocessing import StandardScaler
import json

def normalize_data(df, features):
    scaler = StandardScaler()
    df[features] = scaler.fit_transform(df[features])
    
    # Save normalization parameters for deployment
    params = {
        'mean': scaler.mean_.tolist(),
        'std': scaler.scale_.tolist(),
        'features': features
    }
    
    with open('models/normalization_params.json', 'w') as f:
        json.dump(params, f)
    
    return df, scaler
```

---

### Phase 2: Model Training

#### Option 1: Anomaly Detection (Autoencoder)

```python
# train_anomaly_detector.py
import tensorflow as tf
from tensorflow import keras
import numpy as np

# Prepare data
features = ['pm25', 'pm10', 'co2', 'tvoc', 'temp', 'humidity']
X_train = df_train[features].values
X_val = df_val[features].values

# Build autoencoder
input_dim = len(features)
encoding_dim = 3

model = keras.Sequential([
    keras.layers.Dense(12, activation='relu', input_shape=(input_dim,)),
    keras.layers.Dense(encoding_dim, activation='relu', name='bottleneck'),
    keras.layers.Dense(12, activation='relu'),
    keras.layers.Dense(input_dim, activation='linear')
])

model.compile(optimizer='adam', loss='mse')

# Train
history = model.fit(
    X_train, X_train,
    epochs=50,
    batch_size=32,
    validation_data=(X_val, X_val),
    callbacks=[
        keras.callbacks.EarlyStopping(patience=5, restore_best_weights=True)
    ]
)

# Calculate threshold for anomaly detection
train_pred = model.predict(X_train)
train_mse = np.mean(np.power(X_train - train_pred, 2), axis=1)
threshold = np.percentile(train_mse, 95)  # 95th percentile

print(f"Anomaly threshold: {threshold}")

# Save threshold
with open('models/anomaly_threshold.txt', 'w') as f:
    f.write(str(threshold))

model.save('models/anomaly_detector.h5')
```

#### Option 2: Forecasting (LSTM)

```python
# train_forecaster.py
import tensorflow as tf
from tensorflow import keras
import numpy as np

# Create sequences
def create_sequences(data, seq_length, forecast_horizon):
    X, y = [], []
    for i in range(len(data) - seq_length - forecast_horizon + 1):
        X.append(data[i:i+seq_length])
        y.append(data[i+seq_length:i+seq_length+forecast_horizon])
    return np.array(X), np.array(y)

features = ['pm25', 'pm10', 'co2', 'tvoc', 'temp', 'humidity']
seq_length = 10  # Last 10 readings
forecast_horizon = 1  # Predict next reading

X_train, y_train = create_sequences(df_train[features].values, seq_length, forecast_horizon)
X_val, y_val = create_sequences(df_val[features].values, seq_length, forecast_horizon)

# Build LSTM model
model = keras.Sequential([
    keras.layers.LSTM(16, input_shape=(seq_length, len(features))),
    keras.layers.Dense(len(features))
])

model.compile(optimizer='adam', loss='mse', metrics=['mae'])

# Train
history = model.fit(
    X_train, y_train.squeeze(),
    epochs=30,
    batch_size=32,
    validation_data=(X_val, y_val.squeeze()),
    callbacks=[
        keras.callbacks.EarlyStopping(patience=5, restore_best_weights=True),
        keras.callbacks.ReduceLROnPlateau(patience=3, factor=0.5)
    ]
)

model.save('models/forecaster_lstm.h5')
```

#### Option 3: Classification

```python
# train_classifier.py
import tensorflow as tf
from tensorflow import keras
from sklearn.utils.class_weight import compute_class_weight

features = ['pm25', 'pm10', 'co2', 'tvoc', 'temp', 'humidity']
X_train = df_train[features].values
y_train = df_train['aqi_class'].values

# Handle class imbalance
class_weights = compute_class_weight(
    'balanced',
    classes=np.unique(y_train),
    y=y_train
)
class_weight_dict = dict(enumerate(class_weights))

# Build classifier
model = keras.Sequential([
    keras.layers.Dense(16, activation='relu', input_shape=(len(features),)),
    keras.layers.Dropout(0.3),
    keras.layers.Dense(8, activation='relu'),
    keras.layers.Dense(4, activation='softmax')  # 4 classes
])

model.compile(
    optimizer='adam',
    loss='sparse_categorical_crossentropy',
    metrics=['accuracy']
)

# Train
history = model.fit(
    X_train, y_train,
    epochs=30,
    batch_size=32,
    validation_data=(X_val, y_val),
    class_weight=class_weight_dict,
    callbacks=[
        keras.callbacks.EarlyStopping(patience=5, restore_best_weights=True)
    ]
)

model.save('models/aqi_classifier.h5')
```

---

### Phase 3: Model Optimization for TinyML

#### Convert to TensorFlow Lite

```python
# convert_to_tflite.py
import tensorflow as tf

def convert_model(model_path, output_path, quantize=True):
    # Load Keras model
    model = tf.keras.models.load_model(model_path)
    
    # Convert to TFLite
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    
    if quantize:
        # Quantization for smaller model size
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]
        # For int8 quantization, provide representative dataset
        # converter.representative_dataset = representative_dataset_gen
    
    tflite_model = converter.convert()
    
    # Save
    with open(output_path, 'wb') as f:
        f.write(tflite_model)
    
    # Print model size
    size_kb = len(tflite_model) / 1024
    print(f"Model size: {size_kb:.2f} KB")
    
    return tflite_model

# Convert all models
convert_model('models/anomaly_detector.h5', 'models/anomaly_detector.tflite')
convert_model('models/forecaster_lstm.h5', 'models/forecaster_lstm.tflite')
convert_model('models/aqi_classifier.h5', 'models/aqi_classifier.tflite')
```

#### Generate C Array for ESP32

```python
# tflite_to_c_array.py
def convert_to_c_array(tflite_path, output_path):
    with open(tflite_path, 'rb') as f:
        tflite_model = f.read()
    
    # Convert to C array
    c_array = ', '.join([f'0x{b:02x}' for b in tflite_model])
    
    header = f"""
// Auto-generated TFLite model
// Size: {len(tflite_model)} bytes

#ifndef MODEL_H
#define MODEL_H

const unsigned char model_data[] = {{
{c_array}
}};

const unsigned int model_data_len = {len(tflite_model)};

#endif // MODEL_H
"""
    
    with open(output_path, 'w') as f:
        f.write(header)
    
    print(f"Generated {output_path} ({len(tflite_model)} bytes)")

# Generate headers
convert_to_c_array('models/anomaly_detector.tflite', 'include/anomaly_model.h')
convert_to_c_array('models/aqi_classifier.tflite', 'include/classifier_model.h')
```

---

## Deployment Strategy

### ESP32 Integration

#### Step 1: Install TensorFlow Lite for Microcontrollers

Add to `platformio.ini`:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    tflm-esp32
    # OR
    https://github.com/tensorflow/tflite-micro-arduino-examples
monitor_speed = 115200
```

#### Step 2: Update main.cpp

```cpp
// src/main.cpp
#include <Arduino.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Include model
#include "anomaly_model.h"  // Auto-generated from .tflite

// TFLite globals
namespace {
  tflite::ErrorReporter* error_reporter = nullptr;
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;
  
  // Tensor arena (adjust size based on model)
  constexpr int kTensorArenaSize = 10 * 1024;  // 10KB
  uint8_t tensor_arena[kTensorArenaSize];
}

// Normalization parameters (from training)
const float mean[] = {15.2, 22.5, 650.0, 180.0, 22.0, 45.0};  // Example values
const float std[] = {8.5, 12.3, 250.0, 120.0, 5.0, 15.0};

void setupTFLite() {
  // Set up logging
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;
  
  // Load model
  model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema mismatch!");
    return;
  }
  
  // Build interpreter
  static tflite::AllOpsResolver resolver;
  static tflite::MicroInterpreter static_interpreter(
    model, resolver, tensor_arena, kTensorArenaSize, error_reporter
  );
  interpreter = &static_interpreter;
  
  // Allocate tensors
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    Serial.println("AllocateTensors() failed");
    return;
  }
  
  // Get input/output tensors
  input = interpreter->input(0);
  output = interpreter->output(0);
  
  Serial.println("TFLite model loaded successfully!");
}

bool detectAnomaly(float pm25, float pm10, float co2, float tvoc, float temp, float humidity) {
  // Normalize inputs
  float normalized[6];
  normalized[0] = (pm25 - mean[0]) / std[0];
  normalized[1] = (pm10 - mean[1]) / std[1];
  normalized[2] = (co2 - mean[2]) / std[2];
  normalized[3] = (tvoc - mean[3]) / std[3];
  normalized[4] = (temp - mean[4]) / std[4];
  normalized[5] = (humidity - mean[5]) / std[5];
  
  // Copy to input tensor
  for (int i = 0; i < 6; i++) {
    input->data.f[i] = normalized[i];
  }
  
  // Run inference
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk) {
    Serial.println("Invoke failed");
    return false;
  }
  
  // Calculate reconstruction error (MSE)
  float mse = 0;
  for (int i = 0; i < 6; i++) {
    float diff = normalized[i] - output->data.f[i];
    mse += diff * diff;
  }
  mse /= 6.0;
  
  // Compare to threshold
  const float threshold = 0.5;  // From training
  bool is_anomaly = mse > threshold;
  
  if (is_anomaly) {
    Serial.printf("ANOMALY DETECTED! MSE: %.4f\n", mse);
  }
  
  return is_anomaly;
}

void setup() {
  Serial.begin(115200);
  // ... other setup ...
  
  setupTFLite();
}

void loop() {
  // Read sensors
  float pm25 = readPM25();
  float pm10 = readPM10();
  float co2 = readCO2();
  float tvoc = readTVOC();
  float temp = readTemp();
  float humidity = readHumidity();
  
  // Run inference
  bool anomaly = detectAnomaly(pm25, pm10, co2, tvoc, temp, humidity);
  
  // Send data with anomaly flag
  sendToBackend(pm25, pm10, co2, tvoc, temp, humidity, anomaly);
  
  delay(30000);  // 30 seconds
}
```

---

## Implementation Roadmap

### Phase 1: Data Collection (Weeks 1-4)
- [ ] Collect 2-4 weeks of continuous data from all devices
- [ ] Store in database with proper timestamps
- [ ] Manually label any known anomalies
- [ ] Verify data quality (no missing values, sensor calibration)

**Deliverable**: 50,000+ samples in CSV format

---

### Phase 2: Model Development (Weeks 5-7)

#### Week 5: Setup & Preprocessing
- [ ] Set up Python environment (TensorFlow, scikit-learn, pandas)
- [ ] Export data from backend API
- [ ] Implement feature engineering pipeline
- [ ] Split data (70% train, 15% val, 15% test)
- [ ] Calculate normalization parameters

#### Week 6: Training
- [ ] Train anomaly detection model (autoencoder)
- [ ] Train classification model (AQI categorization)
- [ ] Train forecasting model (LSTM) - optional
- [ ] Evaluate on validation set
- [ ] Tune hyperparameters

#### Week 7: Optimization
- [ ] Convert models to TensorFlow Lite
- [ ] Apply quantization (float16 or int8)
- [ ] Benchmark model size (<50KB each)
- [ ] Generate C header files
- [ ] Validate TFLite model accuracy vs. original

**Deliverable**: TFLite models + normalization params

---

### Phase 3: ESP32 Integration (Weeks 8-9)

#### Week 8: Firmware Development
- [ ] Add TensorFlow Lite library to PlatformIO
- [ ] Include model header files
- [ ] Implement inference functions
- [ ] Add normalization in preprocessing
- [ ] Test on single device

#### Week 9: Testing & Refinement
- [ ] Measure inference time (<100ms)
- [ ] Check memory usage (<100KB RAM)
- [ ] Validate predictions vs. Python model
- [ ] Test with live sensor data
- [ ] Optimize for battery life

**Deliverable**: Updated firmware with TinyML

---

### Phase 4: Backend Integration (Week 10)

- [ ] Update backend API to receive anomaly flags
- [ ] Add endpoint for model performance metrics
- [ ] Store inference results in database
- [ ] Create dashboard visualizations for predictions
- [ ] Implement alert system for anomalies

**Deliverable**: Full end-to-end system

---

### Phase 5: Monitoring & Iteration (Weeks 11+)

- [ ] Monitor model performance in production
- [ ] Collect edge cases and failures
- [ ] Retrain models monthly with new data
- [ ] A/B test model improvements
- [ ] Optimize based on user feedback

---

## Tools & Libraries

### Python (Training)
```bash
pip install tensorflow==2.15.0
pip install scikit-learn pandas numpy matplotlib
pip install jupyter notebook  # For experimentation
```

### ESP32 (Deployment)
- **TensorFlow Lite for Microcontrollers**: Pre-built for ESP32
- **Arduino-ESP32**: Framework
- **PlatformIO**: Build system

### Recommended Hardware
- ESP32 with 4MB Flash, 520KB SRAM (standard ESP32-WROOM-32)
- MicroSD card for logging (optional)

---

## Expected Model Performance

### Anomaly Detection
- **Precision**: 85-90% (few false positives)
- **Recall**: 80-85% (catches most anomalies)
- **False Positive Rate**: <5%

### Classification
- **Accuracy**: 90-95% (4-class AQI)
- **Macro F1**: 0.88-0.92

### Forecasting (15-min ahead)
- **MAE**: ±3-5 µg/m³ for PM2.5
- **RMSE**: ±5-8 µg/m³ for PM2.5

### Resource Usage
- **Model Size**: 10-50KB per model
- **RAM**: 50-100KB during inference
- **Inference Time**: 5-50ms depending on model
- **Power**: <50mA additional (negligible on ESP32)

---

## Troubleshooting

### Model Too Large
- Use float16 quantization instead of float32
- Reduce layer sizes (e.g., LSTM 16→8 units)
- Prune unnecessary features
- Use simpler architectures (decision trees)

### Poor Accuracy
- Collect more diverse data (different conditions)
- Add more features (rolling stats, time features)
- Try ensemble methods
- Check for data leakage

### Slow Inference
- Use quantized int8 models
- Reduce input sequence length (LSTM)
- Use simpler models (feedforward vs. LSTM)
- Optimize tensor arena size

### Memory Issues on ESP32
- Reduce tensor arena size
- Use dynamic allocation carefully
- Run models sequentially, not in parallel
- Consider external PSRAM (ESP32-WROVER)

---

## Next Steps

1. **Start Data Collection Now**: The sooner you start, the sooner you can train models
2. **Prototype in Python**: Test model architectures on historical data first
3. **Iterate Quickly**: Start with simple models, add complexity as needed
4. **Monitor Production**: Model performance degrades over time, plan for retraining

---

## Resources

### Tutorials
- [TensorFlow Lite for Microcontrollers Guide](https://www.tensorflow.org/lite/microcontrollers)
- [ESP32 TinyML Examples](https://github.com/eloquentarduino/EloquentTinyML)
- [Time Series Forecasting with LSTM](https://www.tensorflow.org/tutorials/structured_data/time_series)

### Research Papers
- "Tiny Machine Learning: Progress and Futures" (MIT)
- "Anomaly Detection in IoT Sensor Data" (IEEE)
- "On-Device ML for Air Quality Prediction" (ACM SenSys)

### Community
- [TinyML Foundation](https://www.tinyml.org/)
- [TensorFlow Lite Micro GitHub](https://github.com/tensorflow/tflite-micro)

---

## Conclusion

TinyML can transform your air quality monitoring system from a simple data collector into an intelligent edge device capable of real-time anomaly detection, forecasting, and decision-making. Start with anomaly detection (simplest), then expand to classification and forecasting as you gain experience.

**The key is to start collecting data now** - models are only as good as the data they're trained on!

Good luck! 🚀
