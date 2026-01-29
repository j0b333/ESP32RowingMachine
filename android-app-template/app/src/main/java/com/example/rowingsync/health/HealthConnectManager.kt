package com.example.rowingsync.health

import android.content.Context
import android.util.Log
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.permission.HealthPermission
import androidx.health.connect.client.records.*
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import androidx.health.connect.client.units.Energy
import androidx.health.connect.client.units.Length
import com.example.rowingsync.data.SessionDetail
import java.time.Instant
import java.time.ZoneId

/**
 * Manager for Health Connect integration
 * Handles permissions and writing exercise sessions
 */
class HealthConnectManager(private val context: Context) {
    
    companion object {
        private const val TAG = "HealthConnectManager"
        
        /**
         * Required permissions for the app
         */
        val PERMISSIONS = setOf(
            HealthPermission.getReadPermission(ExerciseSessionRecord::class),
            HealthPermission.getWritePermission(ExerciseSessionRecord::class),
            HealthPermission.getReadPermission(HeartRateRecord::class),
            HealthPermission.getWritePermission(HeartRateRecord::class),
            HealthPermission.getReadPermission(DistanceRecord::class),
            HealthPermission.getWritePermission(DistanceRecord::class),
            HealthPermission.getReadPermission(TotalCaloriesBurnedRecord::class),
            HealthPermission.getWritePermission(TotalCaloriesBurnedRecord::class)
        )
    }
    
    private val healthConnectClient by lazy { HealthConnectClient.getOrCreate(context) }
    
    /**
     * Check if Health Connect is available on this device
     */
    fun isAvailable(): Boolean {
        return HealthConnectClient.getSdkStatus(context) == HealthConnectClient.SDK_AVAILABLE
    }
    
    /**
     * Check if all required permissions are granted
     */
    suspend fun hasAllPermissions(): Boolean {
        return try {
            val granted = healthConnectClient.permissionController.getGrantedPermissions()
            PERMISSIONS.all { it in granted }
        } catch (e: Exception) {
            Log.e(TAG, "Error checking permissions", e)
            false
        }
    }
    
    /**
     * Sync a rowing session to Health Connect
     * 
     * @param session The session detail from ESP32
     * @return true if sync was successful
     */
    suspend fun syncSession(session: SessionDetail): Boolean {
        // Check if Health Connect is available
        if (!isAvailable()) {
            Log.e(TAG, "Health Connect is not available")
            return false
        }
        
        // Check permissions before attempting to write
        if (!hasAllPermissions()) {
            Log.e(TAG, "Missing required Health Connect permissions")
            return false
        }
        
        return try {
            val startTime = Instant.ofEpochMilli(session.startTime)
            val endTime = startTime.plusSeconds(session.duration.toLong())
            val zoneId = ZoneId.systemDefault()
            
            // Create exercise session record (Rowing)
            val exerciseSession = ExerciseSessionRecord(
                startTime = startTime,
                startZoneOffset = zoneId.rules.getOffset(startTime),
                endTime = endTime,
                endZoneOffset = zoneId.rules.getOffset(endTime),
                exerciseType = ExerciseSessionRecord.EXERCISE_TYPE_ROWING_MACHINE,
                title = "Rowing Session"
            )
            
            // Create distance record
            val distanceRecord = DistanceRecord(
                startTime = startTime,
                startZoneOffset = zoneId.rules.getOffset(startTime),
                endTime = endTime,
                endZoneOffset = zoneId.rules.getOffset(endTime),
                distance = Length.meters(session.distance.toDouble())
            )
            
            // Create calories record
            val caloriesRecord = TotalCaloriesBurnedRecord(
                startTime = startTime,
                startZoneOffset = zoneId.rules.getOffset(startTime),
                endTime = endTime,
                endZoneOffset = zoneId.rules.getOffset(endTime),
                energy = Energy.kilocalories(session.calories.toDouble())
            )
            
            // Build list of records to insert
            val records = mutableListOf<Record>(exerciseSession, distanceRecord, caloriesRecord)
            
            // Add heart rate records if available
            if (session.heartRateSamples.isNotEmpty()) {
                val heartRateSamples = session.heartRateSamples.map { sample ->
                    HeartRateRecord.Sample(
                        time = Instant.ofEpochMilli(sample.time),
                        beatsPerMinute = sample.bpm.toLong()
                    )
                }
                
                val heartRateRecord = HeartRateRecord(
                    startTime = startTime,
                    startZoneOffset = zoneId.rules.getOffset(startTime),
                    endTime = endTime,
                    endZoneOffset = zoneId.rules.getOffset(endTime),
                    samples = heartRateSamples
                )
                records.add(heartRateRecord)
            }
            
            // Insert all records
            healthConnectClient.insertRecords(records)
            
            Log.i(TAG, "Successfully synced session ${session.id} to Health Connect")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to sync session to Health Connect", e)
            false
        }
    }
}
