# ESP32 Rowing Monitor - Physics Model Documentation

## Overview

This document explains the physics model used by the ESP32 Rowing Monitor to calculate rowing metrics including power, distance, pace, and calories. It covers all assumptions, configurable parameters, and calibration methods.

## How the Model Works

### Core Principle

The rowing monitor measures the rotation of the flywheel using a reed switch (magnetic sensor). By analyzing how quickly the flywheel spins up during your pull (drive phase) and how quickly it slows down when you're not pulling (recovery phase), the system calculates your power output and other metrics.

### The Physics

When you row:
1. **Drive Phase**: You pull the handle, spinning up the flywheel. Power is transferred from your muscles to the flywheel's rotational energy.
2. **Recovery Phase**: You return to the starting position. The flywheel coasts, slowing down due to air resistance (drag).

The system uses these equations:

```
Angular Velocity (ω) = radians / time
Angular Acceleration (α) = Δω / Δt
Torque (τ) = I × α
Power (P) = (I × α + k × ω²) × ω
```

Where:
- `I` = Moment of Inertia (kg⋅m²) - how "heavy" the flywheel feels rotationally
- `k` = Drag Coefficient - how much air resistance affects the flywheel
- `ω` = Angular Velocity (rad/s) - how fast the flywheel is spinning
- `α` = Angular Acceleration (rad/s²) - how fast the spin is changing

## Configurable Parameters

### 1. Moment of Inertia (`moment_of_inertia`)

**Default**: `0.101 kg⋅m²`  
**Location**: `app_config.h` → `DEFAULT_MOMENT_OF_INERTIA`

**What it is**: A measure of how much torque is needed to change the flywheel's rotational speed. It depends on the flywheel's mass and how that mass is distributed.

**How to estimate without a Concept2**:

1. **Geometric Method**: For a solid disc flywheel:
   ```
   I = 0.5 × m × r²
   ```
   Where:
   - `m` = mass of flywheel in kg (weigh it if possible)
   - `r` = radius of flywheel in meters
   
   Example: A 5kg flywheel with 15cm radius:
   ```
   I = 0.5 × 5 × 0.15² = 0.0563 kg⋅m²
   ```

2. **Spindown Method** (more accurate):
   - Spin the flywheel to a known speed (count revolutions per second)
   - Let it coast and time how long it takes to stop
   - Use the deceleration curve to estimate I

3. **Trial and Error**:
   - Row at what feels like moderate effort
   - Adjust I until displayed power matches expected output:
     - Light rowing: 50-100W
     - Moderate rowing: 100-200W
     - Hard rowing: 200-300W
     - Sprint: 300-500W

### 2. Initial Drag Coefficient (`initial_drag_coefficient`)

**Default**: `0.0001`  
**Location**: `app_config.h` → `DEFAULT_DRAG_COEFFICIENT`

**What it is**: Determines how much the air resistance slows the flywheel. The system auto-calibrates this value during your workout by observing how quickly the flywheel decelerates during recovery phases.

**Auto-Calibration**: After about 50 strokes, the system learns your machine's drag coefficient. You can see the resulting "Drag Factor" in the web UI (displayed on the Row tab).

**Typical Drag Factor ranges** (for reference):
- Light resistance: 90-120
- Medium resistance: 120-150
- Heavy resistance: 150-200+

### 3. Distance Calibration Factor (`distance_calibration_factor`)

**Default**: `2.8` meters  
**Location**: `app_config.h` → `DEFAULT_DISTANCE_PER_REV`

**What it is**: How many meters of "virtual boat travel" per flywheel revolution. This is a calibration factor that makes distances comparable to other rowing machines.

**How to calibrate**:

1. **Compare against a reference**: If you have access to another calibrated rowing machine (or a friend with one), row the same workout and adjust this factor to match distances.

2. **Use known workout benchmarks**:
   - Average rowers cover about 7-10 meters per stroke
   - At 25 strokes per minute, that's 175-250 meters per minute
   - Adjust until your distance matches these expectations

3. **Use the formula**:
   The relationship between power and pace on a Concept2 is:
   ```
   Watts = 2.80 / (pace_per_meter)³
   ```
   If your pace seems too fast or slow relative to power, adjust the distance factor.

### 4. Magnets Per Revolution

**Default**: `1`  
**Location**: `app_config.h` → `DEFAULT_MAGNETS_PER_REV`

**What it is**: How many magnets are attached to your flywheel. Each magnet triggers the reed switch once per revolution.

**Why change it**: Adding more magnets (e.g., 8 instead of 1) provides:
- More precise velocity measurements
- Better stroke detection at low speeds
- Smoother metrics

**How to change**: If you add magnets to your flywheel, update this value to match. The system automatically divides by this number when calculating angular velocity.

### 5. Stroke Detection Thresholds

Located in `app_config.h`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `DRIVE_START_VELOCITY_THRESHOLD` | 15.0 rad/s | Minimum velocity to detect drive start |
| `DRIVE_ACCELERATION_THRESHOLD` | 10.0 rad/s² | Minimum acceleration for drive detection |
| `RECOVERY_VELOCITY_THRESHOLD` | 8.0 rad/s | Velocity below which we detect recovery/idle |
| `MINIMUM_STROKE_DURATION_MS` | 500 ms | Minimum valid stroke time |

**When to adjust**:
- If strokes aren't being detected, lower the thresholds
- If you're getting false strokes, raise the thresholds
- Different flywheel sizes may need different thresholds

## Power Calculation Methods

### Internal Physics Power (used for energy calculations)

The raw physics calculation:
```
Power = (I × α + k × ω²) × ω
```

This gives instantaneous power which can spike to 2000W+ during the drive phase and drop to 0 during recovery. This is used for tracking total work done.

### Display Power (Concept2-Style)

For the user-facing display, we use the Concept2 formula that relates power to pace:
```
Watts = 2.80 / (pace_per_meter)³
```

Where `pace_per_meter = pace_sec_500m / 500`

This provides:
- Stable, smoothed power readings
- Power values that match Concept2 expectations
- No spikes or drops during the stroke cycle

**Example values**:
| Pace (500m) | Power |
|-------------|-------|
| 2:30 | ~104W |
| 2:00 | ~203W |
| 1:45 | ~302W |
| 1:30 | ~500W |

## Distance Calculation

Distance is calculated per stroke:
```
base_distance = distance_calibration_factor (default 2.8m)
power_factor = √(average_power / 100)
distance_this_stroke = base_distance × power_factor
```

This means:
- At 100W average power → 2.8m per stroke
- At 225W average power → 4.2m per stroke
- At 400W average power → 5.6m per stroke

Distance is clamped to 2-20 meters per stroke as a sanity check.

## Pace Calculation

Pace (time per 500 meters) is calculated as:
```
pace_sec_500m = (elapsed_time / total_distance) × 500
```

The displayed pace is the session average. Instantaneous pace is planned for a future update using a rolling window.

## Calorie Calculation

Calories are estimated using:
```
calories = average_power × 0.01433 × elapsed_minutes + (1.0 × elapsed_minutes)
```

Where:
- `0.01433` kcal per watt-minute (standard conversion)
- `1.0` kcal per minute baseline metabolic rate

## Calibration Without a Concept2

If you don't have a Concept2 to compare against, here are practical approaches:

### Method 1: Perceived Effort

1. Start with default values
2. Row at what feels like moderate effort (conversation pace)
3. Check displayed power:
   - Should be 100-200W for most people
   - Adjust `moment_of_inertia` if power seems off:
     - Power too high? Decrease I
     - Power too low? Increase I

### Method 2: Heart Rate Correlation

If you have a heart rate monitor:
1. Row at 65-75% max HR (Zone 2)
2. This typically corresponds to 100-180W depending on fitness
3. Adjust parameters until power matches expectations

### Method 3: Known Workout Benchmark

Row a "standard" 2000m test:
- Beginner: 9-10 minutes (avg ~2:15-2:30/500m, ~130-180W)
- Intermediate: 7.5-9 minutes (avg ~1:52-2:15/500m, ~200-280W)
- Advanced: 6.5-7.5 minutes (avg ~1:37-1:52/500m, ~280-400W)

Adjust distance_calibration_factor if your time doesn't match expected effort level.

### Method 4: Flywheel Measurement

1. Measure your flywheel:
   - Mass (weigh it)
   - Radius (measure from center to edge)
2. Calculate theoretical I: `I = 0.5 × mass × radius²`
3. Start with this value and fine-tune based on feel

## Practical Tips

1. **Let drag calibrate**: Row for at least 50 strokes before trusting the drag factor reading

2. **Consistency matters**: Keep flywheel vents clean - dust changes drag characteristics

3. **Warm up the system**: The first few strokes may have unstable readings as the system calibrates

4. **Track relative progress**: Even if absolute values don't match a Concept2, your improvement over time is valid

5. **Use heart rate**: Adding a Bluetooth heart rate monitor gives you another data point for calibration

## Summary of Key Files

| File | Purpose |
|------|---------|
| `app_config.h` | All compile-time configuration constants |
| `rowing_physics.c` | Core physics calculations |
| `rowing_physics.h` | Data structures and function declarations |
| `stroke_detector.c` | Stroke phase detection algorithm |
| `config_manager.c` | Runtime configuration storage (NVS) |

## References

- [Concept2 Watts Calculator](https://www.concept2.com/indoor-rowers/training/calculators/watts-calculator)
- [OpenRowingMonitor Physics](https://github.com/laberning/openrowingmonitor)
- [Indoor Rowing Physics Paper](https://www.concept2.com/indoor-rowers/training/tips-and-general-info)
