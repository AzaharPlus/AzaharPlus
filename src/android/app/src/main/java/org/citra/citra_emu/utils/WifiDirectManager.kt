// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.wifi.p2p.WifiP2pConfig
import android.net.wifi.p2p.WifiP2pDevice
import android.net.wifi.p2p.WifiP2pManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.annotation.RequiresPermission
import androidx.core.content.ContextCompat
import kotlin.random.Random
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.Log

class WifiDirectManager(private val activity: Activity) {

    interface Listener {
        fun onSearching()
        fun onPeersFound(peers: List<WifiP2pDevice>)
        fun onConnecting(peerName: String)
        fun onSettingUp(isHost: Boolean)
        fun onSuccess(isHost: Boolean)
        fun onError(message: String)
    }

    var listener: Listener? = null

    private val manager: WifiP2pManager? =
        activity.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
    private var channel: WifiP2pManager.Channel? = null
    private var receiver: BroadcastReceiver? = null
    private var isConnecting = false
    private var isComplete = false
    private var retryCount = 0
    private var lastSeenPeer: WifiP2pDevice? = null

    private val timeoutHandler = Handler(Looper.getMainLooper())
    private var connectionTimeoutRunnable: Runnable? = null

    private fun stateSnapshot(): String =
        "isConnecting=$isConnecting, isComplete=$isComplete, retryCount=$retryCount, channel=${if (channel != null) "open" else "null"}"

    private fun scheduleConnectionTimeout() {
        Log.debug("[WifiDirectManager] scheduleConnectionTimeout — replacing existing=${connectionTimeoutRunnable != null}, ${stateSnapshot()}")
        cancelConnectionTimeout()
        connectionTimeoutRunnable = Runnable {
            Log.warning("[WifiDirectManager] Connection timeout fired — ${stateSnapshot()}")
            if (!isComplete && isConnecting) {
                Log.warning("[WifiDirectManager] Connection timed out after ${CONNECTION_TIMEOUT_MS / 1000}s, cancelling connect")
                isConnecting = false
                channel?.let { manager?.cancelConnect(it, null) }
                listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
            } else {
                Log.debug("[WifiDirectManager] Timeout fired but state already resolved (isComplete=$isComplete, isConnecting=$isConnecting), ignoring")
            }
        }.also { timeoutHandler.postDelayed(it, CONNECTION_TIMEOUT_MS) }
        Log.debug("[WifiDirectManager] Connection timeout scheduled for ${CONNECTION_TIMEOUT_MS}ms from now")
    }

    private fun cancelConnectionTimeout() {
        if (connectionTimeoutRunnable != null) {
            Log.debug("[WifiDirectManager] cancelConnectionTimeout — removing pending timeout")
            timeoutHandler.removeCallbacks(connectionTimeoutRunnable!!)
            connectionTimeoutRunnable = null
        } else {
            Log.debug("[WifiDirectManager] cancelConnectionTimeout — no pending timeout to cancel")
        }
    }

    @SuppressLint("MissingPermission")
    private fun retryDiscovery() {
        Log.info("[WifiDirectManager] retryDiscovery called — ${stateSnapshot()}")
        if (isComplete) {
            Log.debug("[WifiDirectManager] retryDiscovery: isComplete=true, aborting")
            return
        }
        Log.info("[WifiDirectManager] Retrying peer discovery (attempt $retryCount/$MAX_RETRIES)")
        val ch = channel
        if (ch == null) {
            Log.error("[WifiDirectManager] retryDiscovery: channel is null, cannot retry")
            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
            return
        }
        try {
            manager?.discoverPeers(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    Log.debug("[WifiDirectManager] Re-discovery started successfully")
                }

                override fun onFailure(reason: Int) {
                    Log.error("[WifiDirectManager] Re-discovery failed (reason=$reason / ${reasonName(reason)}), ${stateSnapshot()}")
                    listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                }
            })
        } catch (e: SecurityException) {
            Log.error("[WifiDirectManager] retryDiscovery: SecurityException — $e")
            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
        }
    }

    fun hasPermission(): Boolean {
        val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.NEARBY_WIFI_DEVICES
        } else {
            Manifest.permission.ACCESS_FINE_LOCATION
        }
        val granted = ContextCompat.checkSelfPermission(activity, permission) ==
            PackageManager.PERMISSION_GRANTED
        Log.debug("[WifiDirectManager] hasPermission: permission=$permission, granted=$granted")
        return granted
    }

    fun getRequiredPermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    @RequiresPermission(allOf = [Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.NEARBY_WIFI_DEVICES])
    fun startDiscovery() {
        Log.info("[WifiDirectManager] startDiscovery — SDK=${Build.VERSION.SDK_INT}, manager=${if (manager != null) "present" else "null"}")
        if (manager == null) {
            Log.error("[WifiDirectManager] Wi-Fi Direct is not supported on this device")
            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_not_supported))
            return
        }

        Log.debug("[WifiDirectManager] Initializing WifiP2pManager channel")
        val ch = manager.initialize(activity, activity.mainLooper, null) ?: run {
            Log.error("[WifiDirectManager] Failed to initialize WifiP2pManager channel")
            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_not_supported))
            return
        }
        channel = ch
        Log.debug("[WifiDirectManager] Channel initialized successfully, registering broadcast receiver")

        val filter = IntentFilter().apply {
            addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
        }
        receiver = WifiDirectReceiver()
        ContextCompat.registerReceiver(activity, receiver!!, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        Log.debug("[WifiDirectManager] Broadcast receiver registered for P2P_STATE, P2P_PEERS, P2P_CONNECTION actions")

        // Check for a pre-existing group first (e.g. established by the OS before the emulator
        // was launched). If one is found, skip discovery entirely and set up the room directly.
        Log.info("[WifiDirectManager] Checking for pre-existing Wi-Fi Direct group")
        manager.requestConnectionInfo(ch) { info ->
            if (isComplete) return@requestConnectionInfo
            if (info.groupFormed) {
                Log.info("[WifiDirectManager] Pre-existing group detected — skipping discovery")
                val hostAddress = info.groupOwnerAddress?.hostAddress
                if (hostAddress == null) {
                    Log.warning("[WifiDirectManager] Pre-existing group but groupOwnerAddress is null, falling back to default $WIFI_DIRECT_HOST_IP")
                }
                onGroupFormed(info.isGroupOwner, hostAddress ?: WIFI_DIRECT_HOST_IP)
            } else {
                Log.info("[WifiDirectManager] No existing group, starting peer discovery")
                try {
                    manager.discoverPeers(ch, object : WifiP2pManager.ActionListener {
                        override fun onSuccess() {
                            Log.info("[WifiDirectManager] Peer discovery started successfully")
                            listener?.onSearching()
                        }

                        override fun onFailure(reason: Int) {
                            Log.error("[WifiDirectManager] Peer discovery failed (reason=$reason / ${reasonName(reason)})")
                            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                        }
                    })
                } catch (e: SecurityException) {
                    Log.error("[WifiDirectManager] startDiscovery: SecurityException — $e")
                    listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                }
            }
        }
    }

    fun stop() {
        Log.info("[WifiDirectManager] stop() called — ${stateSnapshot()}")
        cancelConnectionTimeout()
        // Drain ALL pending callbacks (retry runnables, deferred connects, etc.) to ensure
        // nothing fires against a closed channel after teardown.
        timeoutHandler.removeCallbacksAndMessages(null)
        isComplete = true
        isConnecting = false
        retryCount = 0
        lastSeenPeer = null
        listener = null
        Log.debug("[WifiDirectManager] Unregistering broadcast receiver")
        try {
            receiver?.let { activity.unregisterReceiver(it) }
            Log.debug("[WifiDirectManager] Broadcast receiver unregistered")
        } catch (_: IllegalArgumentException) {
            Log.debug("[WifiDirectManager] Broadcast receiver was not registered (already cleaned up)")
        }
        receiver = null
        Log.debug("[WifiDirectManager] Stopping peer discovery and disconnecting")
        channel?.let { ch ->
            manager?.stopPeerDiscovery(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() = Log.debug("[WifiDirectManager] stopPeerDiscovery succeeded")
                override fun onFailure(reason: Int) = Log.debug("[WifiDirectManager] stopPeerDiscovery failed (reason=$reason / ${reasonName(reason)})")
            })
            manager?.cancelConnect(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() = Log.debug("[WifiDirectManager] cancelConnect succeeded")
                override fun onFailure(reason: Int) = Log.debug("[WifiDirectManager] cancelConnect failed (reason=$reason / ${reasonName(reason)})")
            })
            manager?.removeGroup(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() = Log.debug("[WifiDirectManager] removeGroup succeeded")
                override fun onFailure(reason: Int) = Log.debug("[WifiDirectManager] removeGroup failed (reason=$reason / ${reasonName(reason)})")
            })
        }
        Log.debug("[WifiDirectManager] Closing channel")
        channel?.close()
        channel = null
        Log.info("[WifiDirectManager] Stopped cleanly")
    }

    @RequiresPermission(allOf = [Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.NEARBY_WIFI_DEVICES])
    private fun connectToPeer(device: WifiP2pDevice) {
        Log.info("[WifiDirectManager] connectToPeer: name='${device.deviceName}', address=${device.deviceAddress}, " +
            "status=${deviceStatusName(device.status)}, isGroupOwner=${device.isGroupOwner}")
        Log.debug("[WifiDirectManager] connectToPeer: ${stateSnapshot()}")
        isConnecting = true
        listener?.onConnecting(device.deviceName ?: "another device")
        val intent = if (device.isGroupOwner) 0 else Random.nextInt(0, 16)
        Log.debug("[WifiDirectManager] connectToPeer: groupOwnerIntent=$intent (forced 0 because target isGroupOwner=${device.isGroupOwner})")
        val config = WifiP2pConfig().apply {
            deviceAddress = device.deviceAddress
            // Randomize intent so simultaneous connects resolve deterministically via spec tiebreaker
            // rather than relying on firmware behaviour. The device with higher intent becomes the GO.
            // Force 0 when the target is already a GO so we unambiguously join as a client.
            groupOwnerIntent = intent
        }
        val ch = channel
        if (ch == null) {
            Log.error("[WifiDirectManager] connectToPeer: channel is null, aborting connect")
            isConnecting = false
            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
            return
        }
        try {
            manager?.connect(ch, config, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    Log.debug("[WifiDirectManager] Connect request sent and accepted by framework, awaiting group formation")
                    scheduleConnectionTimeout()
                }

                override fun onFailure(reason: Int) {
                    Log.error("[WifiDirectManager] Failed to connect to peer (reason=$reason / ${reasonName(reason)}), ${stateSnapshot()}")
                    if (reason == WifiP2pManager.BUSY) {
                        // The peer already initiated connection to us. Stay in connecting state and
                        // wait passively — WIFI_P2P_CONNECTION_CHANGED_ACTION will fire when the
                        // group forms. The connection timeout acts as a safety net.
                        Log.info("[WifiDirectManager] Connect BUSY — peer likely already initiated connection to us, waiting passively for group formation")
                        scheduleConnectionTimeout()
                    } else {
                        Log.error("[WifiDirectManager] Non-recoverable connect failure, reporting error")
                        cancelConnectionTimeout()
                        isConnecting = false
                        listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                    }
                }
            })
        } catch (e: SecurityException) {
            Log.error("[WifiDirectManager] connectToPeer: SecurityException — $e")
            isConnecting = false
            listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
        }
    }

    /**
     * Called by the UI when the user selects a peer from the list. Initiates the P2P connection.
     * If both devices happen to select each other simultaneously, the BUSY handler on one side
     * will wait passively for the other's connection to complete.
     */
    @SuppressLint("MissingPermission")
    fun connectToSelectedPeer(device: WifiP2pDevice) {
        Log.info("[WifiDirectManager] connectToSelectedPeer: name='${device.deviceName}', ${stateSnapshot()}")
        if (isConnecting || isComplete) {
            Log.debug("[WifiDirectManager] connectToSelectedPeer: already connecting or complete, ignoring")
            return
        }
        lastSeenPeer = device
        connectToPeer(device)
    }

    private fun onGroupFormed(isGroupOwner: Boolean, groupOwnerAddress: String) {
        Log.info("[WifiDirectManager] onGroupFormed: isGroupOwner=$isGroupOwner, groupOwnerAddress=$groupOwnerAddress, ${stateSnapshot()}")
        if (isComplete) {
            Log.warning("[WifiDirectManager] onGroupFormed called but isComplete=true, ignoring duplicate")
            return
        }
        isComplete = true
        cancelConnectionTimeout()
        Log.info("[WifiDirectManager] Group formed — isGroupOwner=$isGroupOwner, ownerAddress=$groupOwnerAddress")
        listener?.onSettingUp(isGroupOwner)

        val pendingListener = listener
        val port = DEFAULT_PORT
        val username = NetPlayManager.getUsername(activity)
        Log.info("[WifiDirectManager] Starting NetPlay setup thread: username='$username', port=$port, role=${if (isGroupOwner) "HOST" else "CLIENT"}")

        Thread {
            Log.debug("[WifiDirectManager] NetPlay thread started")
            try {
                val result = if (isGroupOwner) {
                    val roomName = activity.getString(R.string.multiplayer_default_room_name, username)
                    Log.info("[WifiDirectManager] Creating room as host: name='$roomName', address=$groupOwnerAddress, port=$port, maxPlayers=$MAX_PLAYERS")
                    NetPlayManager.netPlayCreateRoom(
                        groupOwnerAddress, port, username,
                        WIFI_DIRECT_GAME_NAME, 0L, "", roomName, MAX_PLAYERS
                    )
                } else {
                    Log.info("[WifiDirectManager] Joining room as client: host=$groupOwnerAddress, port=$port, username='$username'")
                    NetPlayManager.netPlayJoinRoom(groupOwnerAddress, port, username, "")
                }
                Log.info("[WifiDirectManager] NetPlay ${if (isGroupOwner) "createRoom" else "joinRoom"} returned: $result")

                activity.runOnUiThread {
                    if (result == NetPlayManager.NetPlayStatus.NO_ERROR) {
                        Log.info("[WifiDirectManager] Room ${if (isGroupOwner) "created" else "joined"} successfully, notifying listener")
                        pendingListener?.onSuccess(isGroupOwner)
                    } else {
                        Log.error("[WifiDirectManager] Room ${if (isGroupOwner) "creation" else "join"} failed (status=$result), notifying error")
                        pendingListener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                    }
                }
            } catch (e: Exception) {
                Log.error("[WifiDirectManager] NetPlay setup thread threw an exception: $e")
                activity.runOnUiThread {
                    pendingListener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                }
            }
        }.start()
    }

    private inner class WifiDirectReceiver : BroadcastReceiver() {
        @RequiresPermission(allOf = [Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.NEARBY_WIFI_DEVICES])
        override fun onReceive(context: Context, intent: Intent) {
            Log.debug("[WifiDirectManager] onReceive: action=${intent.action}, ${stateSnapshot()}")
            when (intent.action) {
                WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                    val state = intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1)
                    val stateName = if (state == WifiP2pManager.WIFI_P2P_STATE_ENABLED) "ENABLED" else "DISABLED ($state)"
                    Log.info("[WifiDirectManager] P2P state changed: $stateName")
                    if (state != WifiP2pManager.WIFI_P2P_STATE_ENABLED) {
                        Log.error("[WifiDirectManager] Wi-Fi P2P disabled (state=$state), aborting connection")
                        cancelConnectionTimeout()
                        isConnecting = false
                        if (!isComplete) listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                        else Log.debug("[WifiDirectManager] P2P disabled but isComplete=true, suppressing error")
                    }
                }
                WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> {
                    Log.debug("[WifiDirectManager] Peers changed broadcast received — isConnecting=$isConnecting")
                    if (isConnecting || isComplete) {
                        Log.debug("[WifiDirectManager] Already connecting or complete, ignoring peers changed event")
                        return
                    }
                    val ch = channel
                    if (ch == null) {
                        Log.error("[WifiDirectManager] Peers changed but channel is null, ignoring")
                        return
                    }
                    manager?.requestPeers(ch) { peerList ->
                        // Guard against stop() running between requestPeers() dispatch and callback.
                        if (isComplete) return@requestPeers
                        val peers = peerList.deviceList.toList()
                        Log.info("[WifiDirectManager] requestPeers result: ${peers.size} peer(s) found")
                        peers.forEachIndexed { i, p ->
                            Log.debug("[WifiDirectManager]   peer[$i]: name='${p.deviceName}', addr=${p.deviceAddress}, " +
                                "status=${deviceStatusName(p.status)}, isGroupOwner=${p.isGroupOwner}, " +
                                "primaryDeviceType=${p.primaryDeviceType}")
                        }

                        // If a peer is in INVITED state, this device received an inbound connection
                        // invitation from the other side. Transition to connecting state immediately
                        // so the UI stops showing the peer list and the user cannot tap another device.
                        val invitedPeer = peers.firstOrNull { it.status == WifiP2pDevice.INVITED }
                        if (invitedPeer != null) {
                            Log.info("[WifiDirectManager] Detected INVITED peer '${invitedPeer.deviceName}' — inbound invitation, entering connecting state")
                            lastSeenPeer = invitedPeer
                            isConnecting = true
                            listener?.onConnecting(invitedPeer.deviceName ?: "another device")
                            scheduleConnectionTimeout()
                            return@requestPeers
                        }

                        listener?.onPeersFound(peers)
                    }
                }
                WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                    Log.debug("[WifiDirectManager] Connection changed broadcast received — ${stateSnapshot()}")
                    if (isComplete) {
                        Log.debug("[WifiDirectManager] isComplete=true, ignoring connection changed event")
                        return
                    }
                    val ch = channel
                    if (ch == null) {
                        Log.error("[WifiDirectManager] Connection changed but channel is null, ignoring")
                        return
                    }
                    manager?.requestConnectionInfo(ch) { info ->
                        Log.info("[WifiDirectManager] requestConnectionInfo: groupFormed=${info.groupFormed}, " +
                            "isGroupOwner=${info.isGroupOwner}, groupOwnerAddress=${info.groupOwnerAddress?.hostAddress ?: "null"}")
                        Log.debug("[WifiDirectManager] Connection info detail — ${stateSnapshot()}")
                        if (info.groupFormed && !isComplete) {
                            val hostAddress = info.groupOwnerAddress?.hostAddress
                            if (hostAddress == null) {
                                Log.warning("[WifiDirectManager] groupOwnerAddress is null, falling back to default $WIFI_DIRECT_HOST_IP")
                            } else {
                                Log.debug("[WifiDirectManager] Group owner address resolved: $hostAddress")
                            }
                            onGroupFormed(info.isGroupOwner, hostAddress ?: WIFI_DIRECT_HOST_IP)
                        } else if (!info.groupFormed && isConnecting && !isComplete) {
                            Log.warning("[WifiDirectManager] Group dissolved before formation completed (groupFormed=false while isConnecting=true)")
                            cancelConnectionTimeout()
                            isConnecting = false
                            if (retryCount < MAX_RETRIES) {
                                retryCount++
                                val cachedPeer = lastSeenPeer
                                if (cachedPeer == null) {
                                    Log.warning("[WifiDirectManager] Connection dropped before group formed, no cached peer — falling back to discovery (retry $retryCount/$MAX_RETRIES)")
                                    listener?.onSearching()
                                    timeoutHandler.postDelayed(::retryDiscovery, RETRY_DELAY_MS)
                                } else {
                                    Log.warning("[WifiDirectManager] Connection dropped before group formed, retrying connect to cached peer '${cachedPeer.deviceName}' ($retryCount/$MAX_RETRIES)")
                                    listener?.onConnecting(cachedPeer.deviceName ?: "another device")
                                    timeoutHandler.postDelayed({ connectToPeer(cachedPeer) }, RETRY_DELAY_MS)
                                }
                            } else {
                                Log.error("[WifiDirectManager] Connection failed after $MAX_RETRIES retries, giving up")
                                listener?.onError(activity.getString(R.string.multiplayer_wifi_direct_error))
                            }
                        } else {
                            Log.debug("[WifiDirectManager] Connection changed: no action needed " +
                                "(groupFormed=${info.groupFormed}, isConnecting=$isConnecting, isComplete=$isComplete)")
                        }
                    }
                }
            }
        }
    }

    companion object {
        private const val DEFAULT_PORT = 24872
        private const val MAX_PLAYERS = 4
        private const val WIFI_DIRECT_HOST_IP = "192.168.49.1"
        private const val WIFI_DIRECT_GAME_NAME = "Wi-Fi Direct Room"
        private const val CONNECTION_TIMEOUT_MS = 30_000L
        private const val MAX_RETRIES = 3
        private const val RETRY_DELAY_MS = 1_000L

        private fun reasonName(reason: Int): String = when (reason) {
            WifiP2pManager.ERROR -> "ERROR"
            WifiP2pManager.P2P_UNSUPPORTED -> "P2P_UNSUPPORTED"
            WifiP2pManager.BUSY -> "BUSY"
            WifiP2pManager.NO_SERVICE_REQUESTS -> "NO_SERVICE_REQUESTS"
            else -> "UNKNOWN($reason)"
        }

        private fun deviceStatusName(status: Int): String = when (status) {
            WifiP2pDevice.CONNECTED -> "CONNECTED"
            WifiP2pDevice.INVITED -> "INVITED"
            WifiP2pDevice.FAILED -> "FAILED"
            WifiP2pDevice.AVAILABLE -> "AVAILABLE"
            WifiP2pDevice.UNAVAILABLE -> "UNAVAILABLE"
            else -> "UNKNOWN($status)"
        }
    }
}
