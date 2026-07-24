package com.wechathub

import android.accessibilityservice.AccessibilityService
import android.app.Notification
import android.os.Handler
import android.os.Looper
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class WeChatBridge : AccessibilityService() {

    companion object {
        private const val TAG = "WeChatHub"
        private const val WECHAT_PKG = "com.tencent.mm"
        val capturedMessages = mutableListOf<String>()
    }

    private val handler = Handler(Looper.getMainLooper())
    private var lastNotificationKey = ""
    private var lastWindowTexts = setOf<String>()
    private var pollInitialized = false

    private val pollRunnable = Runnable { pollWindow() }

    override fun onCreate() {
        super.onCreate()
        // Start polling 2s after service boots
        handler.postDelayed(pollRunnable, 2000)
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent) {
        if (event.packageName != WECHAT_PKG) return

        when (event.eventType) {
            AccessibilityEvent.TYPE_NOTIFICATION_STATE_CHANGED -> {
                captureFromNotification(event)
            }
            // Window event triggers are unreliable for content — poll does the real work
        }
    }

    private fun pollWindow() {
        captureFromWindow()
        handler.postDelayed(pollRunnable, 2000)
    }

    private fun captureFromNotification(event: AccessibilityEvent) {
        val notification = event.parcelableData as? Notification ?: return
        val extras = notification.extras ?: return
        val title = extras.getString(Notification.EXTRA_TITLE)
        val text = extras.getString(Notification.EXTRA_TEXT)
        val sender = title?.takeIf { it.isNotBlank() }
        val content = text?.takeIf { it.isNotBlank() }
        if (sender == null || content == null) return

        val key = "$sender:$content"
        if (key == lastNotificationKey) return
        lastNotificationKey = key

        logMessage("通知", sender, content)
    }

    private fun captureFromWindow() {
        val root = rootInActiveWindow ?: return

        val currentTexts = collectAllLeafTexts(root).toSet()
        if (currentTexts.isEmpty()) return

        // First capture — just save the snapshot
        if (lastWindowTexts.isEmpty()) {
            lastWindowTexts = currentTexts
            return
        }

        // Find new texts
        val newTexts = currentTexts - lastWindowTexts
        if (newTexts.isNotEmpty()) {
            for (text in newTexts) {
                val trimmed = text.trim()
                if (trimmed.isNotBlank() && trimmed.length > 1) {
                    logMessage("界面", "微信", trimmed)
                }
            }
        }

        lastWindowTexts = currentTexts
    }

    private fun collectAllLeafTexts(node: AccessibilityNodeInfo): List<String> {
        val result = mutableListOf<String>()
        if (node.childCount == 0) {
            val t = node.text?.toString()
            if (!t.isNullOrBlank()) result.add(t.trim())
        } else {
            for (i in 0 until node.childCount) {
                val child = node.getChild(i) ?: continue
                result.addAll(collectAllLeafTexts(child))
            }
        }
        return result
    }

    private fun logMessage(source: String, sender: String, content: String) {
        val time = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())
        val line = "[$time][$source] $sender: $content"
        capturedMessages.add(0, line)
        Log.d(TAG, line)
    }

    override fun onInterrupt() {
        Log.d(TAG, "服务中断")
        handler.removeCallbacks(pollRunnable)
    }
}
