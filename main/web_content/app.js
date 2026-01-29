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
    strokeRate: document.getElementById('stroke-rate'),
    strokeCount: document.getElementById('stroke-count'),
    calories: document.getElementById('calories'),
    avgPace: document.getElementById('avg-pace'),
    avgPower: document.getElementById('avg-power'),
    avgStrokeRate: document.getElementById('avg-stroke-rate'),
    dragFactor: document.getElementById('drag-factor'),
    btnReset: document.getElementById('btn-reset'),
    btnSettings: document.getElementById('btn-settings'),
    settingsModal: document.getElementById('settings-modal'),
    settingsForm: document.getElementById('settings-form'),
    btnCloseSettings: document.getElementById('btn-close-settings'),
    userWeight: document.getElementById('user-weight'),
    units: document.getElementById('units'),
    showPower: document.getElementById('show-power'),
    showCalories: document.getElementById('show-calories')
};

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
    
    // Update stroke data
    elements.strokeRate.textContent = data.strokeRate.toFixed(1);
    elements.avgStrokeRate.textContent = data.avgStrokeRate.toFixed(1);
    elements.strokeCount.textContent = data.strokeCount;
    
    // Update calories
    elements.calories.textContent = data.calories;
    
    // Update drag factor
    if (data.dragFactor > 0) {
        elements.dragFactor.textContent = data.dragFactor.toFixed(0);
    } else {
        elements.dragFactor.textContent = '--';
    }
    
    // Update activity status
    if (data.isActive) {
        elements.activityStatus.textContent = 'Active';
        elements.activityStatus.className = 'status active';
        
        // Add pulse animation to pace card
        const paceCard = elements.pace.closest('.metric-card');
        if (paceCard) {
            paceCard.classList.add('active-pulse');
        }
    } else {
        elements.activityStatus.textContent = 'Idle';
        elements.activityStatus.className = 'status idle';
        
        // Remove pulse animation
        const paceCard = elements.pace.closest('.metric-card');
        if (paceCard) {
            paceCard.classList.remove('active-pulse');
        }
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
            elements.connectionStatus.className = 'status connected';
            
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
            elements.connectionStatus.className = 'status disconnected';
            
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
 * Send reset command
 */
async function resetSession() {
    if (!confirm('Are you sure you want to reset the session?')) {
        return;
    }
    
    try {
        const response = await fetch('/api/reset', { method: 'POST' });
        const data = await response.json();
        
        if (data.success) {
            console.log('Session reset');
            // Clear local display immediately
            updateMetrics({
                distance: 0,
                pace: 0,
                avgPace: 0,
                power: 0,
                avgPower: 0,
                strokeRate: 0,
                avgStrokeRate: 0,
                strokeCount: 0,
                calories: 0,
                elapsedTime: 0,
                dragFactor: 0,
                isActive: false
            });
        }
    } catch (e) {
        console.error('Failed to reset session:', e);
        alert('Failed to reset session');
    }
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
            closeSettings();
        }
    } catch (e) {
        console.error('Failed to save settings:', e);
        alert('Failed to save settings');
    }
}

/**
 * Show settings modal
 */
function showSettings() {
    loadSettings();
    elements.settingsModal.classList.remove('hidden');
}

/**
 * Close settings modal
 */
function closeSettings() {
    elements.settingsModal.classList.add('hidden');
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
    // Set up event listeners
    elements.btnReset.addEventListener('click', resetSession);
    elements.btnSettings.addEventListener('click', showSettings);
    elements.btnCloseSettings.addEventListener('click', closeSettings);
    elements.settingsForm.addEventListener('submit', saveSettings);
    
    // Close modal on background click
    elements.settingsModal.addEventListener('click', (e) => {
        if (e.target === elements.settingsModal) {
            closeSettings();
        }
    });
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Start polling as fallback (every 2 seconds)
    setInterval(pollMetrics, 2000);
    
    // Initial poll
    setTimeout(pollMetrics, 500);
    
    console.log('Rowing Monitor client initialized');
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
