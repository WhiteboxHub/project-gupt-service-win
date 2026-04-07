// API base URL
const API_BASE = '/api';

// Auto-refresh interval (5 seconds)
const REFRESH_INTERVAL = 5000;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    refreshHosts();
    refreshStats();

    // Auto-refresh
    setInterval(() => {
        refreshHosts();
        refreshStats();
    }, REFRESH_INTERVAL);
});

// Fetch and display hosts
async function refreshHosts() {
    try {
        const response = await fetch(`${API_BASE}/hosts`);
        const result = await response.json();

        if (result.success) {
            displayHosts(result.data || []);
        } else {
            console.error('Failed to fetch hosts:', result.error);
            showError('Failed to load hosts');
        }
    } catch (error) {
        console.error('Error fetching hosts:', error);
        showError('Network error');
    }
}

// Fetch and display statistics
async function refreshStats() {
    try {
        const response = await fetch(`${API_BASE}/stats`);
        const result = await response.json();

        if (result.success && result.data) {
            updateStats(result.data);
        }
    } catch (error) {
        console.error('Error fetching stats:', error);
    }
}

// Update statistics display
function updateStats(stats) {
    document.getElementById('totalHosts').textContent = stats.total_sessions || 0;
    document.getElementById('waitingHosts').textContent = stats.waiting || 0;
    document.getElementById('connectedHosts').textContent = stats.connected || 0;
    document.getElementById('busyHosts').textContent = stats.busy || 0;
}

// Display hosts
function displayHosts(hosts) {
    const container = document.getElementById('hostsContainer');

    if (hosts.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <h3>No hosts available</h3>
                <p>Waiting for hosts to register...</p>
            </div>
        `;
        return;
    }

    // Sort hosts by state (waiting first, then connected, then busy)
    hosts.sort((a, b) => {
        const stateOrder = { waiting: 0, connected: 1, busy: 2, offline: 3 };
        return (stateOrder[a.state] || 99) - (stateOrder[b.state] || 99);
    });

    container.innerHTML = hosts.map(host => createHostCard(host)).join('');
}

// Create host card HTML
function createHostCard(host) {
    const canConnect = host.state === 'waiting';
    const lastSeen = formatTimestamp(host.last_seen);
    const createdAt = formatTimestamp(host.created_at);

    return `
        <div class="host-card">
            <div class="host-header">
                <div>
                    <span class="host-name">${escapeHtml(host.host_name)}</span>
                    ${host.password_required ? '<span class="password-badge">🔒 Password</span>' : ''}
                </div>
                <span class="host-status ${host.state}">${host.state}</span>
            </div>

            ${host.description ? `<p style="color: #666; margin-bottom: 15px;">${escapeHtml(host.description)}</p>` : ''}

            <div class="host-details">
                <div class="host-detail">
                    <span class="host-detail-label">Session ID</span>
                    <span class="host-detail-value">${escapeHtml(host.session_id)}</span>
                </div>
                <div class="host-detail">
                    <span class="host-detail-label">Host Address</span>
                    <span class="host-detail-value">${escapeHtml(host.host_ip)}</span>
                </div>
                <div class="host-detail">
                    <span class="host-detail-label">TCP Port</span>
                    <span class="host-detail-value">${host.tcp_port}</span>
                </div>
                <div class="host-detail">
                    <span class="host-detail-label">UDP Port</span>
                    <span class="host-detail-value">${host.udp_port}</span>
                </div>
                <div class="host-detail">
                    <span class="host-detail-label">Resolution</span>
                    <span class="host-detail-value">${escapeHtml(host.resolution || 'N/A')}</span>
                </div>
                <div class="host-detail">
                    <span class="host-detail-label">Last Seen</span>
                    <span class="host-detail-value">${lastSeen}</span>
                </div>
            </div>

            ${host.client_ip ? `
                <div style="margin-top: 10px; padding: 10px; background: #e7f3ff; border-radius: 4px;">
                    <strong>Client Connected:</strong> ${escapeHtml(host.client_ip)}
                </div>
            ` : ''}

            <div class="host-actions">
                <button class="connect-btn" onclick="connectToHost('${escapeHtml(host.session_id)}')" ${!canConnect ? 'disabled' : ''}>
                    ${canConnect ? '🚀 Connect' : '⏸️ Unavailable'}
                </button>
                <button class="info-btn" onclick="showHostInfo('${escapeHtml(host.session_id)}')">
                    ℹ️ Details
                </button>
            </div>
        </div>
    `;
}

// Connect to host
function connectToHost(sessionId) {
    const clientId = generateClientId();

    fetch(`${API_BASE}/client/connect`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            session_id: sessionId,
            client_id: clientId,
        }),
    })
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            const host = result.data;
            showConnectionInfo(host, clientId);
        } else {
            alert('Failed to connect: ' + result.error);
        }
    })
    .catch(error => {
        console.error('Error connecting:', error);
        alert('Network error');
    });
}

// Show connection information
function showConnectionInfo(host, clientId) {
    const message = `
Connection established!

Session ID: ${host.session_id}
Host: ${host.host_name}
Address: ${host.host_ip}:${host.tcp_port}

To connect, run the client:
gupt_client.exe --host ${host.host_ip} --port ${host.tcp_port}${host.password_required ? ' --password YOUR_PASSWORD' : ''}

Your Client ID: ${clientId}
    `;

    alert(message);
    refreshHosts();
}

// Show host details
function showHostInfo(sessionId) {
    fetch(`${API_BASE}/host/${sessionId}`)
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            const host = result.data;
            const info = `
Host Information:

Name: ${host.host_name}
Session ID: ${host.session_id}
IP Address: ${host.host_ip}
TCP Port: ${host.tcp_port}
UDP Port: ${host.udp_port}
Resolution: ${host.resolution || 'N/A'}
Password Required: ${host.password_required ? 'Yes' : 'No'}
State: ${host.state}
Created: ${new Date(host.created_at).toLocaleString()}
Last Seen: ${new Date(host.last_seen).toLocaleString()}
${host.client_ip ? `\nClient Connected: ${host.client_ip}` : ''}
${host.description ? `\nDescription: ${host.description}` : ''}
            `;
            alert(info);
        } else {
            alert('Failed to get host info: ' + result.error);
        }
    })
    .catch(error => {
        console.error('Error fetching host info:', error);
        alert('Network error');
    });
}

// Utility functions

function generateClientId() {
    return 'client_' + Math.random().toString(36).substr(2, 9) + '_' + Date.now();
}

function formatTimestamp(timestamp) {
    if (!timestamp) return 'N/A';
    const date = new Date(timestamp);
    const now = new Date();
    const diffSeconds = Math.floor((now - date) / 1000);

    if (diffSeconds < 60) return `${diffSeconds}s ago`;
    if (diffSeconds < 3600) return `${Math.floor(diffSeconds / 60)}m ago`;
    if (diffSeconds < 86400) return `${Math.floor(diffSeconds / 3600)}h ago`;
    return date.toLocaleDateString();
}

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function showError(message) {
    const container = document.getElementById('hostsContainer');
    container.innerHTML = `
        <div class="empty-state">
            <h3>Error</h3>
            <p>${escapeHtml(message)}</p>
        </div>
    `;
}
