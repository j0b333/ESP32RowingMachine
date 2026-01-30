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
    dragFactor: document.getElementById('drag-factor'),
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
    maxHeartRate: document.getElementById('max-heart-rate'),
    momentOfInertia: document.getElementById('moment-of-inertia'),
    units: document.getElementById('units'),
    showPower: document.getElementById('show-power'),
    showCalories: document.getElementById('show-calories'),
    autoPause: document.getElementById('auto-pause'),
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
 * Update control buttons to reflect current state
 */
function updateControlButtons() {
    if (!elements.btnStartPause) return;
    
    if (!workoutRunning) {
        // Not running - show play button
        elements.btnStartPause.textContent = '▶';
        elements.btnStartPause.setAttribute('aria-label', 'Start workout');
        elements.btnStartPause.classList.remove('running', 'paused');
    } else if (workoutPaused) {
        // Running but paused - show play button with paused style
        elements.btnStartPause.textContent = '▶';
        elements.btnStartPause.setAttribute('aria-label', 'Resume workout');
        elements.btnStartPause.classList.remove('running');
        elements.btnStartPause.classList.add('paused');
    } else {
        // Running - show pause button
        elements.btnStartPause.textContent = '⏸';
        elements.btnStartPause.setAttribute('aria-label', 'Pause workout');
        elements.btnStartPause.classList.remove('paused');
        elements.btnStartPause.classList.add('running');
    }
}

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
    // Sync client state with server state (handles auto-start/auto-pause)
    if (data.sessionId !== undefined) {
        const serverHasSession = data.sessionId > 0;
        const serverIsPaused = data.isPaused === true;
        
        // Detect auto-start: server has session, client thinks none is running
        if (serverHasSession && !workoutRunning) {
            console.log('Syncing state: session auto-started on server');
            workoutRunning = true;
            workoutPaused = serverIsPaused;
            updateControlButtons();
        }
        
        // Sync paused state
        if (serverHasSession && workoutRunning && workoutPaused !== serverIsPaused) {
            console.log('Syncing state: paused=' + serverIsPaused);
            workoutPaused = serverIsPaused;
            updateControlButtons();
        }
        
        // Detect session ended: server has no session, client thinks one exists
        if (!serverHasSession && workoutRunning) {
            console.log('Syncing state: session ended on server');
            workoutRunning = false;
            workoutPaused = false;
            updateControlButtons();
        }
    }
    
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
    if (elements.dragFactor) {
        const df = data.dragFactor || 0;
        elements.dragFactor.textContent = df > 0 ? df.toFixed(1) : '--';
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
    
    // Add data point to charts
    addChartDataPoint(data);
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
                
                // Clear chart data
                clearChartData();
                
                // Clear local display immediately
                updateMetrics({
                    distance: 0,
                    pace: 0,
                    avgPace: 0,
                    power: 0,
                    avgPower: 0,
                    dragFactor: 0,
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
        elements.maxHeartRate.value = data.maxHeartRate || 190;
        elements.momentOfInertia.value = data.momentOfInertia || 0.101;
        elements.units.value = data.units || 'metric';
        elements.showPower.checked = data.showPower !== false;
        elements.showCalories.checked = data.showCalories !== false;
        elements.autoPause.value = data.autoPauseSeconds !== undefined ? data.autoPauseSeconds : 5;
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
        maxHeartRate: parseInt(elements.maxHeartRate.value) || 190,
        momentOfInertia: parseFloat(elements.momentOfInertia.value) || 0.101,
        units: elements.units.value,
        showPower: elements.showPower.checked,
        showCalories: elements.showCalories.checked,
        autoPauseSeconds: parseInt(elements.autoPause.value) || 5
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
        loadStorageInfo();
    }
    
    // Load history when switching to history tab
    if (tabName === 'history') {
        loadWorkoutHistory();
    }
    
    // Initialize charts when switching to chart tab
    if (tabName === 'chart') {
        initCharts();
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
    // If timestamp is in microseconds (ESP32 format uses microseconds), convert to milliseconds
    // Microsecond timestamps are around 1.7e15, millisecond timestamps are around 1.7e12
    const ms = timestamp > 1e15 ? timestamp / 1000 : timestamp;
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
            
            const avgHr = session.avgHeartRate ? Math.round(session.avgHeartRate) + ' bpm' : '-- bpm';
            
            item.innerHTML = `
                <div class="history-item-header">
                    <div class="history-item-title">${formatDate(session.startTime)}</div>
                    <button class="btn-delete-history" data-id="${session.id}" aria-label="Delete workout">🗑</button>
                </div>
                <div class="history-item-stats-grid">
                    <div class="history-stat">
                        <span class="stat-label">Duration</span>
                        <span class="stat-value">${formatDuration(session.duration)}</span>
                    </div>
                    <div class="history-stat">
                        <span class="stat-label">Distance</span>
                        <span class="stat-value">${distanceStr}</span>
                    </div>
                    <div class="history-stat">
                        <span class="stat-label">Avg Pace</span>
                        <span class="stat-value">${formatPace(session.avgPace)}</span>
                    </div>
                    <div class="history-stat">
                        <span class="stat-label">Avg Power</span>
                        <span class="stat-value">${Math.round(session.avgPower)} W</span>
                    </div>
                    <div class="history-stat">
                        <span class="stat-label">Strokes</span>
                        <span class="stat-value">${session.strokes}</span>
                    </div>
                    <div class="history-stat">
                        <span class="stat-label">Calories</span>
                        <span class="stat-value">${session.calories} kcal</span>
                    </div>
                    <div class="history-stat">
                        <span class="stat-label">Avg HR</span>
                        <span class="stat-value">${avgHr}</span>
                    </div>
                </div>
                <div class="history-item-action">
                    <span class="view-charts-link">Tap to view charts →</span>
                </div>
            `;
            
            // Add click handler for view charts (not on delete button)
            item.addEventListener('click', (e) => {
                if (!e.target.classList.contains('btn-delete-history')) {
                    showWorkoutCharts(session);
                }
            });
            
            // Add delete button handler
            const deleteBtn = item.querySelector('.btn-delete-history');
            deleteBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                deleteWorkout(session.id);
            });
            
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
 * Show workout charts modal
 */
async function showWorkoutCharts(session) {
    selectedWorkoutId = session.id;
    
    const modal = document.getElementById('workout-detail-modal');
    if (!modal) return;
    
    // Set title to date/time
    const titleEl = document.getElementById('detail-title');
    if (titleEl) {
        titleEl.textContent = formatDate(session.startTime);
    }
    
    // Show modal with loading state
    modal.classList.remove('hidden');
    
    // Draw a chart with actual sample data
    const drawSampleChart = (canvasId, samples, avgValue, label, color, formatFn, isHRChart = false) => {
        const canvas = document.getElementById(canvasId);
        if (!canvas) return;
        
        const ctx = canvas.getContext('2d');
        const dpr = window.devicePixelRatio || 1;
        const rect = canvas.parentElement.getBoundingClientRect();
        
        canvas.width = rect.width * dpr;
        canvas.height = 100 * dpr;
        canvas.style.width = rect.width + 'px';
        canvas.style.height = '100px';
        ctx.scale(dpr, dpr);
        
        const width = rect.width;
        const height = 100;
        const padding = 10;
        
        // Background
        ctx.fillStyle = 'rgba(15, 52, 96, 0.5)';
        ctx.fillRect(0, 0, width, height);
        
        // Find min/max for scaling (needed for HR zones too)
        let minVal = 0, maxVal = 100;
        if (samples && samples.length > 0) {
            minVal = Infinity;
            maxVal = -Infinity;
            for (const val of samples) {
                if (val > 0) {
                    if (val < minVal) minVal = val;
                    if (val > maxVal) maxVal = val;
                }
            }
            if (minVal === Infinity) minVal = 0;
            if (maxVal === -Infinity) maxVal = 100;
        } else if (avgValue > 0) {
            minVal = avgValue * 0.5;
            maxVal = avgValue * 1.5;
        }
        
        // For HR charts, ensure zones are visible
        if (isHRChart) {
            const maxHR = config.maxHR || 200;
            minVal = Math.min(minVal, maxHR * 0.5);  // Show from zone 1
            maxVal = Math.max(maxVal, maxHR);
            
            // Draw HR zone bands
            const hrZones = [
                { min: 0.50, max: 0.60, color: 'rgba(128, 128, 128, 0.3)', label: 'Z1' },
                { min: 0.60, max: 0.70, color: 'rgba(52, 152, 219, 0.3)', label: 'Z2' },
                { min: 0.70, max: 0.80, color: 'rgba(46, 204, 113, 0.3)', label: 'Z3' },
                { min: 0.80, max: 0.90, color: 'rgba(243, 156, 18, 0.3)', label: 'Z4' },
                { min: 0.90, max: 1.00, color: 'rgba(231, 76, 60, 0.3)', label: 'Z5' }
            ];
            
            hrZones.forEach(zone => {
                const zoneMinBPM = maxHR * zone.min;
                const zoneMaxBPM = maxHR * zone.max;
                
                if (zoneMaxBPM >= minVal && zoneMinBPM <= maxVal) {
                    const clampedMin = Math.max(zoneMinBPM, minVal);
                    const clampedMax = Math.min(zoneMaxBPM, maxVal);
                    const range = maxVal - minVal || 1;
                    
                    const yTop = height - padding - ((clampedMax - minVal) / range) * (height - 2 * padding);
                    const yBottom = height - padding - ((clampedMin - minVal) / range) * (height - 2 * padding);
                    
                    ctx.fillStyle = zone.color;
                    ctx.fillRect(padding, yTop, width - 2 * padding, yBottom - yTop);
                }
            });
        }
        
        if (!samples || samples.length === 0) {
            // No data - show average only
            ctx.strokeStyle = color;
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(padding, height / 2);
            ctx.lineTo(width - padding, height / 2);
            ctx.stroke();
            
            ctx.fillStyle = '#fff';
            ctx.font = 'bold 14px sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(`Avg: ${formatFn ? formatFn(avgValue) : avgValue}`, width / 2, height / 2 + 5);
            return;
        }
        
        // Use pre-calculated min/max
        const range = maxVal - minVal || 1;
        
        // Draw the line chart
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        
        const xStep = (width - 2 * padding) / (samples.length - 1 || 1);
        for (let i = 0; i < samples.length; i++) {
            const x = padding + i * xStep;
            const normalizedY = (samples[i] - minVal) / range;
            const y = height - padding - normalizedY * (height - 2 * padding);
            
            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();
        
        // Draw average line (dotted)
        const avgY = height - padding - ((avgValue - minVal) / range) * (height - 2 * padding);
        ctx.setLineDash([4, 4]);
        ctx.strokeStyle = 'rgba(255,255,255,0.5)';
        ctx.beginPath();
        ctx.moveTo(padding, avgY);
        ctx.lineTo(width - padding, avgY);
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Show average label
        ctx.fillStyle = '#fff';
        ctx.font = '11px sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText(`Avg: ${formatFn ? formatFn(avgValue) : avgValue}`, width - padding, avgY - 3);
    };
    
    // Try to fetch detailed session data with samples
    try {
        const response = await fetch(`/api/sessions/${session.id}`);
        if (response.ok) {
            const data = await response.json();
            
            // Draw charts with actual sample data
            setTimeout(() => {
                drawSampleChart('modal-chart-pace', data.paceSamples, data.avgPace, 'Pace', '#16d9e3', formatPace);
                drawSampleChart('modal-chart-power', data.powerSamples, data.avgPower, 'Power', '#96fbc4', v => Math.round(v) + ' W');
                drawSampleChart('modal-chart-hr', data.hrSamples, data.avgHeartRate || 0, 'HR', '#e94560', v => v > 0 ? Math.round(v) + ' bpm' : '-- bpm', true);
                drawSampleChart('modal-chart-spm', data.spmSamples, data.avgStrokeRate || 0, 'SPM', '#fbbf24', v => v > 0 ? v.toFixed(1) + ' spm' : '-- spm');
            }, 50);
        } else {
            throw new Error('Failed to fetch session');
        }
    } catch (e) {
        console.error('Failed to load session details:', e);
        // Fallback to showing just averages
        setTimeout(() => {
            drawSampleChart('modal-chart-pace', null, session.avgPace, 'Pace', '#16d9e3', formatPace);
            drawSampleChart('modal-chart-power', null, session.avgPower, 'Power', '#96fbc4', v => Math.round(v) + ' W');
            drawSampleChart('modal-chart-hr', null, session.avgHeartRate || 0, 'HR', '#e94560', v => v > 0 ? Math.round(v) + ' bpm' : '-- bpm', true);
            drawSampleChart('modal-chart-spm', null, session.avgStrokeRate || 0, 'SPM', '#fbbf24', v => v > 0 ? v.toFixed(1) + ' spm' : '-- spm');
        }, 50);
    }
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
 * Delete workout by ID (called from history list)
 */
function deleteWorkout(workoutId) {
    showConfirmDialog('Delete Workout', `Are you sure you want to delete this workout? This cannot be undone.`, async () => {
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
 * Delete selected workout (legacy - kept for compatibility)
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

// ============================================================================
// Storage Info Functions
// ============================================================================

/**
 * Load storage info and update display
 */
async function loadStorageInfo() {
    try {
        const response = await fetch('/api/sessions');
        if (!response.ok) return;
        
        const data = await response.json();
        
        let totalSeconds = 0;
        if (data.sessions && data.sessions.length > 0) {
            data.sessions.forEach(session => {
                totalSeconds += session.duration || 0;
            });
        }
        
        const totalHours = totalSeconds / 3600;
        const maxHours = 30;
        const percentage = Math.min((totalHours / maxHours) * 100, 100);
        
        const storageUsedEl = document.getElementById('storage-used');
        const storageBarEl = document.getElementById('storage-bar');
        
        if (storageUsedEl) {
            storageUsedEl.textContent = totalHours.toFixed(1);
        }
        if (storageBarEl) {
            storageBarEl.style.width = percentage + '%';
            // Change color based on usage
            if (percentage > 80) {
                storageBarEl.style.background = 'var(--danger)';
            } else if (percentage > 60) {
                storageBarEl.style.background = 'var(--warning)';
            } else {
                storageBarEl.style.background = 'var(--accent-primary)';
            }
        }
    } catch (e) {
        console.error('Failed to load storage info:', e);
    }
}

// ============================================================================
// Chart Functions
// ============================================================================

// Chart data storage
const chartData = {
    pace: [],
    power: [],
    hr: [],
    spm: [],
    timestamps: []
};

// Chart contexts
let charts = {
    pace: null,
    power: null,
    hr: null,
    spm: null
};

// Chart initialized flag
let chartsInitialized = false;

// Chart resize listener added flag
let chartResizeListenerAdded = false;

/**
 * Initialize chart canvases
 */
function initCharts() {
    if (chartsInitialized) {
        renderAllCharts();
        return;
    }
    
    // Get canvas elements
    const paceCanvas = document.getElementById('chart-pace');
    const powerCanvas = document.getElementById('chart-power');
    const hrCanvas = document.getElementById('chart-hr');
    const spmCanvas = document.getElementById('chart-spm');
    
    if (paceCanvas && powerCanvas && hrCanvas && spmCanvas) {
        charts.pace = paceCanvas.getContext('2d');
        charts.power = powerCanvas.getContext('2d');
        charts.hr = hrCanvas.getContext('2d');
        charts.spm = spmCanvas.getContext('2d');
        chartsInitialized = true;
        
        // Set up canvas sizing
        resizeCharts();
        
        // Only add resize listener once
        if (!chartResizeListenerAdded) {
            window.addEventListener('resize', resizeCharts);
            chartResizeListenerAdded = true;
        }
        
        // Add target input listeners for immediate visual feedback
        const targetInputs = ['target-pace', 'target-power', 'target-hr', 'target-spm'];
        targetInputs.forEach(id => {
            const input = document.getElementById(id);
            if (input) {
                input.addEventListener('input', () => {
                    if (chartsInitialized) {
                        renderAllCharts();
                    }
                });
            }
        });
        
        renderAllCharts();
    }
}

/**
 * Resize charts to fit container
 */
function resizeCharts() {
    const canvases = document.querySelectorAll('.chart-canvas');
    canvases.forEach(canvas => {
        const container = canvas.parentElement;
        const dpr = window.devicePixelRatio || 1;
        const rect = container.getBoundingClientRect();
        
        canvas.width = rect.width * dpr;
        canvas.height = 120 * dpr;
        canvas.style.width = rect.width + 'px';
        canvas.style.height = '120px';
        
        const ctx = canvas.getContext('2d');
        ctx.scale(dpr, dpr);
    });
    
    if (chartsInitialized) {
        renderAllCharts();
    }
}

/**
 * Add data point to charts
 */
function addChartDataPoint(data) {
    const elapsedTime = data.elapsedTime || 0;
    
    // Only add data if we have elapsed time
    if (elapsedTime > 0) {
        chartData.timestamps.push(elapsedTime);
        chartData.pace.push(data.pace || 0);
        chartData.power.push(data.power || 0);
        chartData.hr.push(data.heartRate || 0);
        chartData.spm.push(data.strokeRate || 0);
        
        // Limit data points to prevent memory issues (keep last 2 hours of seconds)
        const maxPoints = 7200;
        if (chartData.timestamps.length > maxPoints) {
            chartData.timestamps.shift();
            chartData.pace.shift();
            chartData.power.shift();
            chartData.hr.shift();
            chartData.spm.shift();
        }
    }
    
    // Update current value displays
    const paceValue = document.getElementById('chart-pace-value');
    const powerValue = document.getElementById('chart-power-value');
    const hrValue = document.getElementById('chart-hr-value');
    const spmValue = document.getElementById('chart-spm-value');
    
    if (paceValue) paceValue.textContent = formatPace(data.pace || 0);
    if (powerValue) powerValue.textContent = Math.round(data.power || 0) + ' W';
    if (hrValue) hrValue.textContent = (data.heartRate || '--') + ' bpm';
    if (spmValue) spmValue.textContent = (data.strokeRate || 0).toFixed(1) + ' spm';
    
    // Render charts if on chart tab
    if (currentTab === 'chart' && chartsInitialized) {
        renderAllCharts();
    }
}

/**
 * Clear chart data
 */
function clearChartData() {
    chartData.pace = [];
    chartData.power = [];
    chartData.hr = [];
    chartData.spm = [];
    chartData.timestamps = [];
    
    if (chartsInitialized) {
        renderAllCharts();
    }
}

/**
 * Render all charts
 */
function renderAllCharts() {
    const targetPace = parseFloat(document.getElementById('target-pace')?.value) || null;
    const targetPower = parseFloat(document.getElementById('target-power')?.value) || null;
    const targetHr = parseFloat(document.getElementById('target-hr')?.value) || null;
    const targetSpm = parseFloat(document.getElementById('target-spm')?.value) || null;
    
    renderChart('chart-pace', chartData.pace, chartData.timestamps, '#16d9e3', targetPace, true);
    renderChart('chart-power', chartData.power, chartData.timestamps, '#96fbc4', targetPower);
    renderChart('chart-hr', chartData.hr, chartData.timestamps, '#e94560', targetHr, false, true);
    renderChart('chart-spm', chartData.spm, chartData.timestamps, '#fbbf24', targetSpm);
}

/**
 * Render a single chart
 */
function renderChart(canvasId, dataArray, timestamps, color, target = null, invertY = false, isHRChart = false) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    const width = canvas.width / (window.devicePixelRatio || 1);
    const height = canvas.height / (window.devicePixelRatio || 1);
    
    // Clear canvas
    ctx.clearRect(0, 0, width, height);
    
    // Background
    ctx.fillStyle = 'rgba(15, 52, 96, 0.5)';
    ctx.fillRect(0, 0, width, height);
    
    // Calculate time range (5-minute intervals)
    const maxTime = timestamps.length > 0 ? Math.max(...timestamps) : 0;
    const timeRange = Math.max(300, Math.ceil(maxTime / 300) * 300); // At least 5 minutes, round up to 5-min intervals
    
    // Calculate Y-axis range
    let minVal = 0;
    let maxVal = 100;
    
    if (dataArray.length > 0) {
        const validData = dataArray.filter(v => v > 0);
        if (validData.length > 0) {
            minVal = Math.min(...validData) * 0.8;
            maxVal = Math.max(...validData) * 1.2;
        }
    }
    
    // Apply target to range if set
    if (target !== null) {
        minVal = Math.min(minVal, target * 0.8);
        maxVal = Math.max(maxVal, target * 1.2);
    }
    
    // Ensure reasonable range
    if (maxVal - minVal < 10) {
        maxVal = minVal + 20;
    }
    
    const padding = { left: 45, right: 10, top: 10, bottom: 25 };
    const chartWidth = width - padding.left - padding.right;
    const chartHeight = height - padding.top - padding.bottom;
    
    // Draw HR zone bands if this is an HR chart
    if (isHRChart) {
        const maxHR = config.maxHR || 200;
        // HR Zones (% of max HR) with colors
        const hrZones = [
            { min: 0.50, max: 0.60, color: 'rgba(128, 128, 128, 0.3)', label: 'Z1' },  // Gray - Recovery
            { min: 0.60, max: 0.70, color: 'rgba(52, 152, 219, 0.3)', label: 'Z2' },   // Blue - Fat Burn
            { min: 0.70, max: 0.80, color: 'rgba(46, 204, 113, 0.3)', label: 'Z3' },   // Green - Aerobic
            { min: 0.80, max: 0.90, color: 'rgba(243, 156, 18, 0.3)', label: 'Z4' },   // Orange - Anaerobic
            { min: 0.90, max: 1.00, color: 'rgba(231, 76, 60, 0.3)', label: 'Z5' }     // Red - Maximum
        ];
        
        hrZones.forEach(zone => {
            const zoneMinBPM = maxHR * zone.min;
            const zoneMaxBPM = maxHR * zone.max;
            
            // Only draw if zone is visible in current Y range
            if (zoneMaxBPM >= minVal && zoneMinBPM <= maxVal) {
                const clampedMin = Math.max(zoneMinBPM, minVal);
                const clampedMax = Math.min(zoneMaxBPM, maxVal);
                
                const yTop = height - padding.bottom - ((clampedMax - minVal) / (maxVal - minVal)) * chartHeight;
                const yBottom = height - padding.bottom - ((clampedMin - minVal) / (maxVal - minVal)) * chartHeight;
                
                ctx.fillStyle = zone.color;
                ctx.fillRect(padding.left, yTop, chartWidth, yBottom - yTop);
                
                // Draw zone label on left edge if zone is large enough
                if (yBottom - yTop > 15) {
                    ctx.fillStyle = 'rgba(255, 255, 255, 0.6)';
                    ctx.font = 'bold 10px sans-serif';
                    ctx.textAlign = 'left';
                    ctx.fillText(zone.label, padding.left + 3, (yTop + yBottom) / 2 + 3);
                }
            }
        });
    }
    
    // Draw grid lines and time labels
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
    ctx.lineWidth = 1;
    ctx.fillStyle = 'rgba(255, 255, 255, 0.5)';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    
    // Time grid (every 5 minutes)
    const intervalSeconds = 300;
    for (let t = 0; t <= timeRange; t += intervalSeconds) {
        const x = padding.left + (t / timeRange) * chartWidth;
        
        ctx.beginPath();
        ctx.moveTo(x, padding.top);
        ctx.lineTo(x, height - padding.bottom);
        ctx.stroke();
        
        // Time label
        const minutes = Math.floor(t / 60);
        ctx.fillText(minutes + 'm', x, height - 5);
    }
    
    // Draw Y-axis labels
    ctx.textAlign = 'right';
    const ySteps = 4;
    for (let i = 0; i <= ySteps; i++) {
        const val = minVal + ((maxVal - minVal) * i / ySteps);
        const y = invertY 
            ? padding.top + (i / ySteps) * chartHeight
            : height - padding.bottom - (i / ySteps) * chartHeight;
        
        ctx.fillText(Math.round(val), padding.left - 5, y + 3);
        
        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(width - padding.right, y);
        ctx.stroke();
    }
    
    // Draw target line (dotted)
    if (target !== null && target > 0) {
        const targetY = invertY
            ? padding.top + ((target - minVal) / (maxVal - minVal)) * chartHeight
            : height - padding.bottom - ((target - minVal) / (maxVal - minVal)) * chartHeight;
        
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 5]);
        ctx.beginPath();
        ctx.moveTo(padding.left, targetY);
        ctx.lineTo(width - padding.right, targetY);
        ctx.stroke();
        ctx.setLineDash([]);
    }
    
    // Draw data line
    if (dataArray.length > 1) {
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        
        let firstPoint = true;
        for (let i = 0; i < dataArray.length; i++) {
            const val = dataArray[i];
            if (val <= 0) continue;
            
            const x = padding.left + (timestamps[i] / timeRange) * chartWidth;
            const normalizedVal = (val - minVal) / (maxVal - minVal);
            const y = invertY
                ? padding.top + normalizedVal * chartHeight
                : height - padding.bottom - normalizedVal * chartHeight;
            
            if (firstPoint) {
                ctx.moveTo(x, y);
                firstPoint = false;
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();
        
        // Fill area under the line - convert hex color to rgba
        let fillColor;
        if (color.startsWith('#')) {
            const r = parseInt(color.slice(1, 3), 16);
            const g = parseInt(color.slice(3, 5), 16);
            const b = parseInt(color.slice(5, 7), 16);
            fillColor = `rgba(${r}, ${g}, ${b}, 0.2)`;
        } else {
            fillColor = color.replace(')', ', 0.2)').replace('rgb', 'rgba');
        }
        ctx.fillStyle = fillColor;
        
        // Redraw path for fill
        ctx.beginPath();
        firstPoint = true;
        for (let i = 0; i < dataArray.length; i++) {
            const val = dataArray[i];
            if (val <= 0) continue;
            
            const x = padding.left + (timestamps[i] / timeRange) * chartWidth;
            const normalizedVal = (val - minVal) / (maxVal - minVal);
            const y = invertY
                ? padding.top + normalizedVal * chartHeight
                : height - padding.bottom - normalizedVal * chartHeight;
            
            if (firstPoint) {
                ctx.moveTo(x, y);
                firstPoint = false;
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.lineTo(padding.left + (timestamps[timestamps.length - 1] / timeRange) * chartWidth, 
                   invertY ? padding.top : height - padding.bottom);
        ctx.lineTo(padding.left, invertY ? padding.top : height - padding.bottom);
        ctx.closePath();
        ctx.fill();
    }
    
    // Draw "No data" message if empty
    if (dataArray.length === 0) {
        ctx.fillStyle = 'rgba(255, 255, 255, 0.3)';
        ctx.font = '14px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('Start rowing to see data', width / 2, height / 2);
    }
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
