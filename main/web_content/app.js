/**
 * Crivit Rowing Monitor - Client Application
 */

// WebSocket connection
let ws = null;
let reconnectTimeout = null;
let isConnected = false;

// DOM Elements
const elements = {
    connectionStatus: document.getElementById('connection-status'),
    activityStatus: document.getElementById('activity-status'),
    distance: document.getElementById('distance'),
    elapsedTime: document.getElementById('elapsed-time'),
    pace: document.getElementById('pace'),
    power: document.getElementById('power'),
    peakPower: document.getElementById('peak-power'),
    strokeRate: document.getElementById('stroke-rate'),
    strokeCount: document.getElementById('stroke-count'),
    calories: document.getElementById('calories'),
    avgPace: document.getElementById('avg-pace'),
    avgPower: document.getElementById('avg-power'),
    avgStrokeRate: document.getElementById('avg-stroke-rate'),
    avgHeartRate: document.getElementById('avg-heart-rate'),
    heartRate: document.getElementById('heart-rate'),
    hrStatus: document.getElementById('hr-status'),
    hrCard: document.getElementById('hr-card'),
    btnStartPause: document.getElementById('btn-start-pause'),
    btnResetWorkout: document.getElementById('btn-reset-workout'),
    btnFinish: document.getElementById('btn-finish'),
    settingsForm: document.getElementById('settings-form'),
    settingsFeedback: document.getElementById('settings-feedback'),
    userWeight: document.getElementById('user-weight'),
    units: document.getElementById('units'),
    showPower: document.getElementById('show-power'),
    showCalories: document.getElementById('show-calories'),
    confirmModal: document.getElementById('confirm-modal'),
    confirmTitle: document.getElementById('confirm-title'),
    confirmMessage: document.getElementById('confirm-message'),
    btnConfirmYes: document.getElementById('btn-confirm-yes'),
    btnConfirmNo: document.getElementById('btn-confirm-no'),
    // Tab elements
    tabRow: document.getElementById('tab-row'),
    tabChart: document.getElementById('tab-chart'),
    tabHistory: document.getElementById('tab-history'),
    tabSettings: document.getElementById('tab-settings')
};

// Workout state
let workoutRunning = false;
let workoutPaused = false;
let confirmCallback = null;
let currentTab = 'row';

/**
 * Format time in seconds to MM:SS or HH:MM:SS
 */
function formatTime(totalSeconds) {
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = Math.floor(totalSeconds % 60);
    
    if (hours > 0) {
        return `${hours}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
    }
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
}

/**
 * Format pace in seconds to MM:SS.s
 */
function formatPace(paceSeconds) {
    if (paceSeconds > 9999 || paceSeconds <= 0) {
        return '--:--.-';
    }
    const minutes = Math.floor(paceSeconds / 60);
    const seconds = paceSeconds % 60;
    return `${minutes}:${seconds.toFixed(1).padStart(4, '0')}`;
}

/**
 * Format distance
 */
function formatDistance(meters) {
    if (meters >= 1000) {
        return (meters / 1000).toFixed(2) + ' km';
    }
    return Math.round(meters).toString();
}

/**
 * Update the UI with new metrics
 */
function updateMetrics(data) {
    // Update distance
    elements.distance.textContent = Math.round(data.distance);
    
    // Update elapsed time
    elements.elapsedTime.textContent = formatTime(data.elapsedTime);
    
    // Update pace
    elements.pace.textContent = data.paceStr || formatPace(data.pace);
    elements.avgPace.textContent = data.avgPaceStr || formatPace(data.avgPace);
    
    // Update power
    elements.power.textContent = Math.round(data.power);
    elements.avgPower.textContent = Math.round(data.avgPower);
    if (elements.peakPower) {
        elements.peakPower.textContent = Math.round(data.peakPower || 0);
    }
    
    // Update stroke data
    elements.strokeRate.textContent = data.strokeRate.toFixed(1);
    elements.avgStrokeRate.textContent = data.avgStrokeRate.toFixed(1);
    elements.strokeCount.textContent = data.strokeCount;
    
    // Update calories
    elements.calories.textContent = data.calories;
    
    // Update heart rate display
    updateHeartRate(data);
    
    // Update average heart rate
    if (elements.avgHeartRate) {
        if (data.avgHeartRate && data.avgHeartRate > 0) {
            elements.avgHeartRate.textContent = Math.round(data.avgHeartRate);
        } else {
            elements.avgHeartRate.textContent = '--';
        }
    }
    
    // Update activity status (hidden but tracked for internal state)
    if (data.isActive) {
        elements.activityStatus.textContent = 'Active';
        
        // Add pulse animation to pace card
        const paceCard = elements.pace.closest('.metric-card');
        if (paceCard) {
            paceCard.classList.add('active-pulse');
        }
    } else {
        elements.activityStatus.textContent = 'Idle';
        
        // Remove pulse animation
        const paceCard = elements.pace.closest('.metric-card');
        if (paceCard) {
            paceCard.classList.remove('active-pulse');
        }
    }
}

/**
 * Update heart rate display
 */
function updateHeartRate(data) {
    if (!elements.hrCard) return;
    
    const hrStatus = data.hrStatus || 'idle';
    const heartRate = data.heartRate || 0;
    const hrValid = data.hrValid || false;
    
    // Remove all status classes first
    elements.hrCard.classList.remove('hr-connected', 'hr-scanning', 'hr-disconnected');
    
    if (hrStatus === 'connected' && hrValid && heartRate > 0) {
        // Connected and receiving data
        elements.heartRate.textContent = heartRate;
        elements.hrStatus.textContent = 'Connected';
        elements.hrCard.classList.add('hr-connected');
    } else if (hrStatus === 'connected' && !hrValid) {
        // Connected but data is stale
        elements.heartRate.textContent = heartRate > 0 ? heartRate : '--';
        elements.hrStatus.textContent = 'Signal lost';
        elements.hrCard.classList.add('hr-disconnected');
    } else if (hrStatus === 'scanning') {
        elements.heartRate.textContent = '--';
        elements.hrStatus.textContent = 'Scanning...';
        elements.hrCard.classList.add('hr-scanning');
    } else if (hrStatus === 'connecting') {
        elements.heartRate.textContent = '--';
        elements.hrStatus.textContent = 'Connecting...';
        elements.hrCard.classList.add('hr-scanning');
    } else if (hrStatus === 'error') {
        elements.heartRate.textContent = '--';
        elements.hrStatus.textContent = 'Error';
        elements.hrCard.classList.add('hr-disconnected');
    } else {
        // Idle - no HR monitor
        elements.heartRate.textContent = '--';
        elements.hrStatus.textContent = 'No HR monitor';
        elements.hrCard.classList.add('hr-disconnected');
    }
}

/**
 * Connect to WebSocket
 */
function connectWebSocket() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        return;
    }
    
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    
    console.log('Connecting to WebSocket:', wsUrl);
    
    try {
        ws = new WebSocket(wsUrl);
        
        ws.onopen = () => {
            console.log('WebSocket connected');
            isConnected = true;
            elements.connectionStatus.textContent = 'Connected';
            
            if (reconnectTimeout) {
                clearTimeout(reconnectTimeout);
                reconnectTimeout = null;
            }
        };
        
        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                updateMetrics(data);
            } catch (e) {
                console.error('Error parsing message:', e);
            }
        };
        
        ws.onclose = () => {
            console.log('WebSocket closed');
            isConnected = false;
            elements.connectionStatus.textContent = 'Disconnected';
            
            // Attempt to reconnect after delay
            if (!reconnectTimeout) {
                reconnectTimeout = setTimeout(() => {
                    reconnectTimeout = null;
                    connectWebSocket();
                }, 3000);
            }
        };
        
        ws.onerror = (error) => {
            console.error('WebSocket error:', error);
            ws.close();
        };
        
    } catch (e) {
        console.error('WebSocket connection failed:', e);
        
        // Attempt to reconnect after delay
        if (!reconnectTimeout) {
            reconnectTimeout = setTimeout(() => {
                reconnectTimeout = null;
                connectWebSocket();
            }, 3000);
        }
    }
}

/**
 * Show confirmation modal
 */
function showConfirmDialog(title, message, callback) {
    elements.confirmTitle.textContent = title;
    elements.confirmMessage.textContent = message;
    confirmCallback = callback;
    elements.confirmModal.classList.remove('hidden');
}

/**
 * Hide confirmation modal
 */
function hideConfirmDialog() {
    elements.confirmModal.classList.add('hidden');
    confirmCallback = null;
}

/**
 * Toggle Start/Stop workout
 */
async function toggleStartPause() {
    // Disable button to prevent double-clicks
    elements.btnStartPause.disabled = true;
    
    try {
        if (!workoutRunning) {
            // Start workout
            const response = await fetch('/workout/start', { method: 'POST' });
            if (!response.ok) throw new Error('Server error');
            const data = await response.json();
            if (data.status === 'started') {
                workoutRunning = true;
                workoutPaused = false;
                elements.btnStartPause.textContent = '⏸';
                elements.btnStartPause.setAttribute('aria-label', 'Pause workout');
                elements.btnStartPause.classList.add('running');
            }
        } else if (!workoutPaused) {
            // Pause workout - call server to stop recording metrics
            const response = await fetch('/workout/pause', { method: 'POST' });
            if (!response.ok) throw new Error('Server error');
            const data = await response.json();
            // Accept paused or already_paused status
            if (data.status === 'paused' || data.status === 'already_paused') {
                workoutPaused = true;
                elements.btnStartPause.textContent = '▶';
                elements.btnStartPause.setAttribute('aria-label', 'Resume workout');
                elements.btnStartPause.classList.remove('running');
                elements.btnStartPause.classList.add('paused');
            }
        } else {
            // Resume workout - call server to resume recording metrics
            const response = await fetch('/workout/resume', { method: 'POST' });
            if (!response.ok) throw new Error('Server error');
            const data = await response.json();
            // Accept resumed or not_paused status
            if (data.status === 'resumed' || data.status === 'not_paused') {
                workoutPaused = false;
                elements.btnStartPause.textContent = '⏸';
                elements.btnStartPause.setAttribute('aria-label', 'Pause workout');
                elements.btnStartPause.classList.remove('paused');
                elements.btnStartPause.classList.add('running');
            }
        }
    } catch (e) {
        console.error('Failed to control workout:', e);
        alert('Failed to control workout. Please check connection.');
    } finally {
        elements.btnStartPause.disabled = false;
    }
}

/**
 * Reset workout with confirmation
 */
function resetWorkout() {
    showConfirmDialog('Reset Workout', 'Are you sure you want to reset the workout? All data will be lost.', async () => {
        try {
            const response = await fetch('/api/reset', { method: 'POST' });
            if (!response.ok) throw new Error('Server error');
            const data = await response.json();
            
            if (data.success) {
                console.log('Workout reset');
                workoutRunning = false;
                workoutPaused = false;
                elements.btnStartPause.textContent = '▶';
                elements.btnStartPause.setAttribute('aria-label', 'Start workout');
                elements.btnStartPause.classList.remove('running', 'paused');
                
                // Clear local display immediately
                updateMetrics({
                    distance: 0,
                    pace: 0,
                    avgPace: 0,
                    power: 0,
                    avgPower: 0,
                    peakPower: 0,
                    strokeRate: 0,
                    avgStrokeRate: 0,
                    strokeCount: 0,
                    calories: 0,
                    elapsedTime: 0,
                    isActive: false,
                    heartRate: 0,
                    avgHeartRate: 0,
                    hrValid: false,
                    hrStatus: 'idle'
                });
            }
        } catch (e) {
            console.error('Failed to reset workout:', e);
            alert('Failed to reset workout');
        }
    });
}

/**
 * Finish workout with confirmation (saves for export)
 */
function finishWorkout() {
    if (!workoutRunning) {
        alert('No active workout to finish.');
        return;
    }
    
    showConfirmDialog('Finish Workout', 'Are you sure you want to finish and save this workout?', async () => {
        try {
            // Stop the workout and save
            const stopResponse = await fetch('/workout/stop', { method: 'POST' });
            if (!stopResponse.ok) throw new Error('Server error');
            const stopData = await stopResponse.json();
            
            if (stopData.status === 'stopped') {
                console.log('Workout finished and saved, session #' + stopData.sessionId);
                workoutRunning = false;
                workoutPaused = false;
                elements.btnStartPause.textContent = '▶';
                elements.btnStartPause.setAttribute('aria-label', 'Start workout');
                elements.btnStartPause.classList.remove('running', 'paused');
                
                alert('Workout saved! Session #' + stopData.sessionId + '\nDistance: ' + Math.round(stopData.distance) + 'm');
            }
        } catch (e) {
            console.error('Failed to finish workout:', e);
            alert('Failed to finish workout');
        }
    });
}

/**
 * Load settings from server
 */
async function loadSettings() {
    try {
        const response = await fetch('/api/config');
        const data = await response.json();
        
        elements.userWeight.value = data.userWeight || 75;
        elements.units.value = data.units || 'metric';
        elements.showPower.checked = data.showPower !== false;
        elements.showCalories.checked = data.showCalories !== false;
    } catch (e) {
        console.error('Failed to load settings:', e);
    }
}

/**
 * Show settings feedback message
 */
function showSettingsFeedback(message, isSuccess) {
    if (!elements.settingsFeedback) return;
    
    elements.settingsFeedback.textContent = message;
    elements.settingsFeedback.className = 'settings-feedback ' + (isSuccess ? 'success' : 'error');
    
    // Auto-hide after 3 seconds
    setTimeout(() => {
        elements.settingsFeedback.textContent = '';
        elements.settingsFeedback.className = 'settings-feedback';
    }, 3000);
}

/**
 * Save settings to server
 */
async function saveSettings(event) {
    event.preventDefault();
    
    const settings = {
        userWeight: parseFloat(elements.userWeight.value),
        units: elements.units.value,
        showPower: elements.showPower.checked,
        showCalories: elements.showCalories.checked
    };
    
    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(settings)
        });
        
        const data = await response.json();
        
        if (data.success) {
            console.log('Settings saved');
            showSettingsFeedback('Settings saved successfully!', true);
        }
    } catch (e) {
        console.error('Failed to save settings:', e);
        showSettingsFeedback('Failed to save settings', false);
    }
}

/**
 * Switch to a tab (non-destructive - does not affect workout state)
 */
function switchTab(tabName) {
    // Don't switch to disabled tabs (only chart is disabled now)
    if (tabName === 'chart') {
        return;
    }
    
    // Hide all tab content
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });
    
    // Remove active class from all tab buttons
    document.querySelectorAll('.btn-tab').forEach(btn => {
        btn.classList.remove('active');
    });
    
    // Show selected tab content
    const tabContent = document.getElementById('tab-' + tabName);
    if (tabContent) {
        tabContent.classList.add('active');
    }
    
    // Mark the button as active
    const tabButton = document.querySelector(`.btn-tab[data-tab="${tabName}"]`);
    if (tabButton) {
        tabButton.classList.add('active');
    }
    
    currentTab = tabName;
    
    // Load settings when switching to settings tab
    if (tabName === 'settings') {
        loadSettings();
    }
    
    // Load history when switching to history tab
    if (tabName === 'history') {
        loadWorkoutHistory();
    }
}

// ============================================================================
// History Tab Functions
// ============================================================================

let selectedWorkoutId = null;

/**
 * Format duration in seconds to HH:MM:SS or MM:SS
 */
function formatDuration(seconds) {
    const hours = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);
    
    if (hours > 0) {
        return `${hours}:${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }
    return `${mins}:${secs.toString().padStart(2, '0')}`;
}

/**
 * Format date from timestamp
 */
function formatDate(timestamp) {
    // If timestamp is in microseconds (ESP32 format), convert to milliseconds
    const ms = timestamp > 1e12 ? timestamp / 1000 : timestamp;
    const date = new Date(ms);
    
    // If the date is invalid or before 2020, it's likely an ESP32 uptime timestamp
    if (isNaN(date.getTime()) || date.getFullYear() < 2020) {
        return 'Session';
    }
    
    return date.toLocaleDateString() + ' ' + date.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
}

/**
 * Load workout history from server
 */
async function loadWorkoutHistory() {
    const loadingEl = document.getElementById('history-loading');
    const emptyEl = document.getElementById('history-empty');
    const listEl = document.getElementById('history-list');
    
    if (!loadingEl || !emptyEl || !listEl) return;
    
    // Show loading
    loadingEl.classList.remove('hidden');
    emptyEl.classList.add('hidden');
    listEl.classList.add('hidden');
    
    try {
        const response = await fetch('/api/sessions');
        if (!response.ok) {
            throw new Error('Server returned ' + response.status);
        }
        const data = await response.json();
        
        loadingEl.classList.add('hidden');
        
        if (!data.sessions || data.sessions.length === 0) {
            emptyEl.classList.remove('hidden');
            return;
        }
        
        // Render workout list
        listEl.innerHTML = '';
        data.sessions.forEach(session => {
            const item = document.createElement('div');
            item.className = 'history-item';
            item.dataset.id = session.id;
            
            const distanceStr = session.distance >= 1000 
                ? (session.distance / 1000).toFixed(2) + ' km'
                : Math.round(session.distance) + ' m';
            
            item.innerHTML = `
                <div class="history-item-main">
                    <div class="history-item-title">Workout #${session.id}</div>
                    <div class="history-item-date">${formatDate(session.startTime)}</div>
                </div>
                <div class="history-item-stats">
                    <span class="stat">${distanceStr}</span>
                    <span class="stat">${formatDuration(session.duration)}</span>
                    <span class="stat">${Math.round(session.avgPower)} W</span>
                </div>
            `;
            
            item.addEventListener('click', () => showWorkoutDetail(session));
            listEl.appendChild(item);
        });
        
        listEl.classList.remove('hidden');
        
    } catch (e) {
        console.error('Failed to load workout history:', e);
        loadingEl.classList.add('hidden');
        emptyEl.innerHTML = '<p>Failed to load history</p><p>Check your connection</p>';
        emptyEl.classList.remove('hidden');
    }
}

/**
 * Show workout detail modal
 */
function showWorkoutDetail(session) {
    selectedWorkoutId = session.id;
    
    const modal = document.getElementById('workout-detail-modal');
    if (!modal) return;
    
    // Helper to safely set element text content
    const setElement = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.textContent = value;
    };
    
    // Populate detail fields
    setElement('detail-title', `Workout #${session.id}`);
    setElement('detail-date', formatDate(session.startTime));
    setElement('detail-duration', formatDuration(session.duration));
    
    const distanceStr = session.distance >= 1000 
        ? (session.distance / 1000).toFixed(2) + ' km'
        : Math.round(session.distance) + ' m';
    setElement('detail-distance', distanceStr);
    
    setElement('detail-pace', formatPace(session.avgPace));
    setElement('detail-power', Math.round(session.avgPower) + ' W');
    setElement('detail-strokes', session.strokes);
    setElement('detail-calories', session.calories + ' kcal');
    setElement('detail-drag', session.dragFactor ? session.dragFactor.toFixed(1) : '--');
    
    modal.classList.remove('hidden');
}

/**
 * Close workout detail modal
 */
function closeWorkoutDetail() {
    const modal = document.getElementById('workout-detail-modal');
    if (modal) {
        modal.classList.add('hidden');
    }
    selectedWorkoutId = null;
}

/**
 * Delete selected workout
 */
function deleteSelectedWorkout() {
    if (!selectedWorkoutId) return;
    
    const workoutId = selectedWorkoutId;
    closeWorkoutDetail();
    
    showConfirmDialog('Delete Workout', `Are you sure you want to delete Workout #${workoutId}? This cannot be undone.`, async () => {
        try {
            const response = await fetch(`/api/sessions/${workoutId}`, { method: 'DELETE' });
            const data = await response.json();
            
            if (data.success) {
                console.log('Workout deleted:', workoutId);
                loadWorkoutHistory(); // Refresh the list
            } else {
                alert('Failed to delete workout');
            }
        } catch (e) {
            console.error('Failed to delete workout:', e);
            alert('Failed to delete workout. Please check connection.');
        }
    });
}

/**
 * Poll metrics via REST API (fallback when WebSocket unavailable)
 */
async function pollMetrics() {
    if (isConnected) {
        return;  // WebSocket is active, no need to poll
    }
    
    try {
        const response = await fetch('/api/metrics');
        const data = await response.json();
        updateMetrics(data);
    } catch (e) {
        console.error('Failed to poll metrics:', e);
    }
}

/**
 * Initialize the application
 */
function init() {
    // Set up workout control event listeners
    elements.btnStartPause.addEventListener('click', toggleStartPause);
    elements.btnResetWorkout.addEventListener('click', resetWorkout);
    elements.btnFinish.addEventListener('click', finishWorkout);
    
    // Settings form event listener
    elements.settingsForm.addEventListener('submit', saveSettings);
    
    // Tab navigation event listeners
    document.querySelectorAll('.btn-tab').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const tabName = e.target.dataset.tab;
            if (tabName && !e.target.classList.contains('disabled')) {
                switchTab(tabName);
            }
        });
    });
    
    // Confirmation modal event listeners
    elements.btnConfirmYes.addEventListener('click', () => {
        if (confirmCallback) {
            confirmCallback();
        }
        hideConfirmDialog();
    });
    elements.btnConfirmNo.addEventListener('click', hideConfirmDialog);
    
    // Close modal on background click
    elements.confirmModal.addEventListener('click', (e) => {
        if (e.target === elements.confirmModal) {
            hideConfirmDialog();
        }
    });
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Start polling as fallback (every 2 seconds)
    setInterval(pollMetrics, 2000);
    
    // Initial poll
    setTimeout(pollMetrics, 500);
    
    // Workout detail modal event listeners
    const btnDeleteWorkout = document.getElementById('btn-delete-workout');
    const btnCloseDetail = document.getElementById('btn-close-detail');
    const workoutDetailModal = document.getElementById('workout-detail-modal');
    
    if (btnDeleteWorkout) {
        btnDeleteWorkout.addEventListener('click', deleteSelectedWorkout);
    }
    if (btnCloseDetail) {
        btnCloseDetail.addEventListener('click', closeWorkoutDetail);
    }
    if (workoutDetailModal) {
        workoutDetailModal.addEventListener('click', (e) => {
            if (e.target === workoutDetailModal) {
                closeWorkoutDetail();
            }
        });
    }
    
    // Close all modals on Escape key (consolidated handler)
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            // Close confirm dialog if open
            if (!elements.confirmModal.classList.contains('hidden')) {
                hideConfirmDialog();
                return;
            }
            // Close workout detail modal if open
            const detailModal = document.getElementById('workout-detail-modal');
            if (detailModal && !detailModal.classList.contains('hidden')) {
                closeWorkoutDetail();
            }
        }
    });
    
    console.log('Rowing Monitor client initialized');
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
