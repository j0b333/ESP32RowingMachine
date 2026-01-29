package com.example.rowingsync.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.example.rowingsync.data.SessionSummary
import com.example.rowingsync.ui.MainUiState
import com.example.rowingsync.ui.MainViewModel
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SessionListScreen(viewModel: MainViewModel) {
    val uiState by viewModel.uiState.collectAsState()
    var addressInput by remember { mutableStateOf(uiState.esp32Address) }
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("RowingSync") },
                actions = {
                    IconButton(onClick = { viewModel.refreshSessions() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                    if (uiState.sessions.any { !it.synced }) {
                        IconButton(onClick = { viewModel.syncAllSessions() }) {
                            Icon(Icons.Default.CloudUpload, contentDescription = "Sync All")
                        }
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
        ) {
            // Connection settings
            ConnectionCard(
                address = addressInput,
                onAddressChange = { addressInput = it },
                onConnect = { 
                    viewModel.setEsp32Address(addressInput)
                    viewModel.refreshSessions()
                },
                isConnected = uiState.isConnected,
                isLoading = uiState.isLoading
            )
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Error message
            uiState.error?.let { error ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer
                    )
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            Icons.Default.Error,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onErrorContainer
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = error,
                            color = MaterialTheme.colorScheme.onErrorContainer,
                            modifier = Modifier.weight(1f)
                        )
                        IconButton(onClick = { viewModel.clearError() }) {
                            Icon(Icons.Default.Close, contentDescription = "Dismiss")
                        }
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))
            }
            
            // Health Connect status
            if (!uiState.healthConnectAvailable) {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.tertiaryContainer
                    )
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(Icons.Default.HealthAndSafety, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Health Connect not available on this device")
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))
            }
            
            // Sessions list
            if (uiState.isLoading) {
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            } else if (uiState.sessions.isEmpty()) {
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(
                            Icons.Default.DirectionsBoat,
                            contentDescription = null,
                            modifier = Modifier.size(64.dp),
                            tint = MaterialTheme.colorScheme.outline
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            "No sessions found",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.outline
                        )
                    }
                }
            } else {
                LazyColumn {
                    items(uiState.sessions) { session ->
                        SessionCard(
                            session = session,
                            isSyncing = uiState.syncingSessionId == session.id,
                            onSync = { viewModel.syncSession(session.id) },
                            healthConnectAvailable = uiState.healthConnectAvailable
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                    }
                }
            }
        }
    }
}

@Composable
fun ConnectionCard(
    address: String,
    onAddressChange: (String) -> Unit,
    onConnect: () -> Unit,
    isConnected: Boolean,
    isLoading: Boolean
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    if (isConnected) Icons.Default.Wifi else Icons.Default.WifiOff,
                    contentDescription = null,
                    tint = if (isConnected) MaterialTheme.colorScheme.primary 
                           else MaterialTheme.colorScheme.outline
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    if (isConnected) "Connected" else "Not Connected",
                    style = MaterialTheme.typography.titleMedium
                )
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = address,
                    onValueChange = onAddressChange,
                    label = { Text("ESP32 Address") },
                    modifier = Modifier.weight(1f),
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Uri,
                        imeAction = ImeAction.Done
                    ),
                    keyboardActions = KeyboardActions(
                        onDone = { onConnect() }
                    )
                )
                
                Spacer(modifier = Modifier.width(8.dp))
                
                Button(
                    onClick = onConnect,
                    enabled = !isLoading
                ) {
                    if (isLoading) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp
                        )
                    } else {
                        Text("Connect")
                    }
                }
            }
        }
    }
}

@Composable
fun SessionCard(
    session: SessionSummary,
    isSyncing: Boolean,
    onSync: () -> Unit,
    healthConnectAvailable: Boolean
) {
    val dateFormat = remember { SimpleDateFormat("MMM d, yyyy HH:mm", Locale.getDefault()) }
    
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    dateFormat.format(Date(session.startTime)),
                    style = MaterialTheme.typography.titleMedium
                )
                
                if (session.synced) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            Icons.Default.CloudDone,
                            contentDescription = "Synced",
                            modifier = Modifier.size(20.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            "Synced",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                MetricItem(
                    icon = Icons.Default.Straighten,
                    value = String.format("%.0f m", session.distance),
                    label = "Distance"
                )
                MetricItem(
                    icon = Icons.Default.Timer,
                    value = formatDuration(session.duration),
                    label = "Duration"
                )
                MetricItem(
                    icon = Icons.Default.Rowing,
                    value = "${session.strokes}",
                    label = "Strokes"
                )
                MetricItem(
                    icon = Icons.Default.LocalFireDepartment,
                    value = "${session.calories}",
                    label = "Calories"
                )
            }
            
            if (!session.synced && healthConnectAvailable) {
                Spacer(modifier = Modifier.height(12.dp))
                
                Button(
                    onClick = onSync,
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !isSyncing
                ) {
                    if (isSyncing) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp,
                            color = MaterialTheme.colorScheme.onPrimary
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                    }
                    Icon(Icons.Default.CloudUpload, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(if (isSyncing) "Syncing..." else "Sync to Health Connect")
                }
            }
        }
    }
}

@Composable
fun MetricItem(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    value: String,
    label: String
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Icon(
            icon,
            contentDescription = null,
            modifier = Modifier.size(24.dp),
            tint = MaterialTheme.colorScheme.primary
        )
        Text(
            value,
            style = MaterialTheme.typography.titleSmall
        )
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.outline
        )
    }
}

fun formatDuration(seconds: Int): String {
    val hours = seconds / 3600
    val minutes = (seconds % 3600) / 60
    val secs = seconds % 60
    
    return if (hours > 0) {
        String.format("%d:%02d:%02d", hours, minutes, secs)
    } else {
        String.format("%d:%02d", minutes, secs)
    }
}
