package com.example.rowingsync.data

/**
 * Data models for ESP32 API responses
 */

data class StatusResponse(
    val online: Boolean,
    val workoutInProgress: Boolean,
    val sessionCount: Int,
    val currentHeartRate: Int,
    val freeHeap: Long,
    val uptime: Long,
    val bleConnected: Boolean = false,
    val wsClients: Int = 0,
    // Optional fields when workout in progress
    val currentSessionId: Int? = null,
    val currentDistance: Float? = null,
    val currentStrokes: Int? = null,
    val currentPower: Float? = null,
    val currentPace: Float? = null,
    val currentStrokeRate: Int? = null,
    val duration: Long? = null,
    val hrSamples: Int? = null
)

data class SessionSummary(
    val id: Int,
    val startTime: Long,
    val duration: Int,
    val distance: Float,
    val strokes: Int,
    val calories: Int,
    val avgPower: Float,
    val avgPace: Float,
    val avgHeartRate: Int = 0,
    val maxHeartRate: Int = 0,
    val synced: Boolean = false,
    val hrSampleCount: Int = 0,
    val dragFactor: Float = 0f
)

data class SessionsResponse(
    val sessions: List<SessionSummary>
)

data class HRSample(
    val time: Long,
    val bpm: Int
)

data class SessionDetail(
    val id: Int,
    val startTime: Long,
    val duration: Int,
    val distance: Float,
    val strokes: Int,
    val calories: Int,
    val avgPower: Float,
    val avgPace: Float,
    val avgHeartRate: Int = 0,
    val maxHeartRate: Int = 0,
    val synced: Boolean = false,
    val dragFactor: Float = 0f,
    val heartRateSamples: List<HRSample> = emptyList()
)

data class LiveData(
    val sessionId: Int,
    val distance: Float,
    val strokes: Int,
    val duration: Long,
    val power: Float,
    val pace: Float,
    val strokeRate: Float,
    val heartRate: Int,
    val phase: String,
    val avgPace: Float? = null,
    val avgPower: Float? = null
)

data class WorkoutResponse(
    val status: String,
    val sessionId: Int? = null,
    val distance: Float? = null,
    val strokes: Int? = null,
    val calories: Int? = null,
    val hrSamples: Int? = null,
    val avgHeartRate: Int? = null,
    val maxHeartRate: Int? = null
)

data class GenericResponse(
    val status: String? = null,
    val success: Boolean? = null,
    val error: String? = null
)
