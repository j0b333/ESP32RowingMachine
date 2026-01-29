package com.example.rowingsync.ui

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.rowingsync.data.ApiClient
import com.example.rowingsync.data.SessionSummary
import com.example.rowingsync.health.HealthConnectManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * UI state for the main screen
 */
data class MainUiState(
    val isLoading: Boolean = false,
    val isConnected: Boolean = false,
    val sessions: List<SessionSummary> = emptyList(),
    val error: String? = null,
    val esp32Address: String = "192.168.4.1",
    val syncingSessionId: Int? = null,
    val healthConnectAvailable: Boolean = false
)

/**
 * ViewModel for the main activity
 */
class MainViewModel(application: Application) : AndroidViewModel(application) {
    
    companion object {
        private const val TAG = "MainViewModel"
    }
    
    private val _uiState = MutableStateFlow(MainUiState())
    val uiState: StateFlow<MainUiState> = _uiState.asStateFlow()
    
    private val healthConnectManager = HealthConnectManager(application)
    
    init {
        // Check Health Connect availability
        _uiState.value = _uiState.value.copy(
            healthConnectAvailable = healthConnectManager.isAvailable()
        )
    }
    
    /**
     * Update ESP32 address
     */
    fun setEsp32Address(address: String) {
        _uiState.value = _uiState.value.copy(esp32Address = address)
        ApiClient.reset()
    }
    
    /**
     * Connect to ESP32 and load sessions
     */
    fun refreshSessions() {
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            
            try {
                val api = ApiClient.getApi(_uiState.value.esp32Address)
                val status = api.getStatus()
                
                if (status.online) {
                    val sessionsResponse = api.getSessions()
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        isConnected = true,
                        sessions = sessionsResponse.sessions,
                        error = null
                    )
                } else {
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        isConnected = false,
                        error = "ESP32 reports it is not ready"
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to connect to ESP32", e)
                _uiState.value = _uiState.value.copy(
                    isLoading = false,
                    isConnected = false,
                    error = "Failed to connect: ${e.message}"
                )
            }
        }
    }
    
    /**
     * Sync a single session to Health Connect
     */
    fun syncSession(sessionId: Int) {
        // Check if Health Connect is available before attempting sync
        if (!_uiState.value.healthConnectAvailable) {
            _uiState.value = _uiState.value.copy(
                error = "Health Connect is not available on this device"
            )
            return
        }
        
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(syncingSessionId = sessionId)
            
            try {
                val api = ApiClient.getApi(_uiState.value.esp32Address)
                val sessionDetail = api.getSession(sessionId)
                
                val success = healthConnectManager.syncSession(sessionDetail)
                
                if (success) {
                    // Mark as synced on ESP32
                    try {
                        val markResponse = api.markSynced(sessionId)
                        if (markResponse.error != null) {
                            Log.w(TAG, "Failed to mark session as synced on ESP32: ${markResponse.error}")
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "Failed to mark session as synced on ESP32", e)
                        // Continue anyway since the Health Connect sync succeeded
                    }
                    
                    // Refresh session list
                    refreshSessions()
                } else {
                    _uiState.value = _uiState.value.copy(
                        error = "Failed to sync to Health Connect. Please check permissions."
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to sync session", e)
                _uiState.value = _uiState.value.copy(
                    error = "Sync failed: ${e.message}"
                )
            } finally {
                _uiState.value = _uiState.value.copy(syncingSessionId = null)
            }
        }
    }
    
    /**
     * Sync all unsynced sessions
     */
    fun syncAllSessions() {
        viewModelScope.launch {
            val unsyncedSessions = _uiState.value.sessions.filter { !it.synced }
            
            for (session in unsyncedSessions) {
                syncSession(session.id)
            }
        }
    }
    
    /**
     * Clear error message
     */
    fun clearError() {
        _uiState.value = _uiState.value.copy(error = null)
    }
}
