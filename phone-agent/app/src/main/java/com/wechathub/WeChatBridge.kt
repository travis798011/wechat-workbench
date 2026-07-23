package com.wechathub

import android.accessibilityservice.AccessibilityService
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.os.Handler
import android.os.Looper
import android.util.Log

class WeChatBridge : AccessibilityService() {

    companion object {
        private const val TAG = "WeChatHub"
        private const val WECHAT_PKG = "com.tencent.mm"
        private const val CLAWBOT_NAME = "ClawBot"

        // UI 节点 ID (不同微信版本可能需要调整)
        private const val ID_CHAT_LIST = "com.tencent.mm:id/b8a"  // 聊天列表
        private const val ID_CHAT_TITLE = "com.tencent.mm:id/gd"  // 聊天标题栏
        private const val ID_MSG_LIST = "com.tencent.mm:id/b8_"   // 消息列表
        private const val ID_INPUT = "com.tencent.mm:id/b4f"      // 输入框
        private const val ID_SEND = "com.tencent.mm:id/b4p"       // 发送按钮
        private const val ID_SEARCH = "com.tencent.mm:id/g_"      // 搜索框 (主界面)
        private const val ID_SEARCH_RESULT = "com.tencent.mm:id/b8x" // 搜索结果
    }

    // 状态
    private enum class State {
        IDLE,
        DETECTED_NEW_MESSAGE,
        GOING_TO_HOME,
        SEARCHING_CLAWBOT,
        TYPING_IN_CLAWBOT,
        GOING_BACK
    }

    private var state = State.IDLE
    private var pendingSender = ""
    private var pendingContent = ""
    private var isProcessing = false
    private val handler = Handler(Looper.getMainLooper())
    private var lastProcessedHash = 0  // 防重复

    override fun onAccessibilityEvent(event: AccessibilityEvent) {
        if (event.packageName != WECHAT_PKG) return
        if (!isProcessing) {
            detectNewMessage(event)
        }
    }

    private fun detectNewMessage(event: AccessibilityEvent) {
        val root = rootInActiveWindow ?: return
        val title = findChatTitle(root)
        if (title == null || title == CLAWBOT_NAME) return

        val messages = findMessageList(root)
        val lastMsg = messages.lastOrNull() ?: return
        val msgHash = (title + lastMsg).hashCode()
        if (msgHash == lastProcessedHash) return

        // 检查是不是自己发的
        if (isSelfSentMessage(root, lastMsg)) return

        lastProcessedHash = msgHash
        pendingSender = title
        pendingContent = lastMsg
        isProcessing = true

        Log.d(TAG, "检测到新消息: [$pendingSender] $pendingContent")
        forwardToClawBot(root)
    }

    private fun forwardToClawBot(root: AccessibilityNodeInfo) {
        // 1. 点击返回到主界面
        performGlobalAction(GLOBAL_ACTION_BACK)
        handler.postDelayed({
            // 2. 点击搜索
            val searchBox = findNodeById(root, "com.tencent.mm:id/f8y")
            searchBox?.let {
                val parent = it.parent
                parent?.performAction(AccessibilityNodeInfo.ACTION_CLICK)
            }
            handler.postDelayed({
                // 3. 输入 ClawBot
                val searchInput = findNodeById(root, "com.tencent.mm:id/g_s")
                searchInput?.let {
                    it.performAction(AccessibilityNodeInfo.ACTION_FOCUS)
                    it.text?.let { currentText ->
                        it.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT)
                    }
                    // 使用剪贴板粘贴
                    val clipboard = getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager
                    clipboard.setPrimaryClip(android.content.ClipData.newPlainText("", CLAWBOT_NAME))
                    it.performAction(AccessibilityNodeInfo.ACTION_PASTE)
                }
                handler.postDelayed({
                    // 4. 点击 ClawBot 搜索结果
                    val clawbotItem = findClawbotItem(root)
                    clawbotItem?.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                    handler.postDelayed({
                        // 5. 输入消息
                        val inputBox = findNodeById(root, ID_INPUT)
                        inputBox?.let {
                            it.performAction(AccessibilityNodeInfo.ACTION_FOCUS)
                            val clipboard = getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager
                            val msg = "$pendingSender: $pendingContent"
                            clipboard.setPrimaryClip(android.content.ClipData.newPlainText("", msg))
                            it.performAction(AccessibilityNodeInfo.ACTION_PASTE)
                        }
                        handler.postDelayed({
                            // 6. 点击发送
                            val sendBtn = findNodeById(root, ID_SEND)
                            sendBtn?.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                            handler.postDelayed({
                                isProcessing = false
                                pendingSender = ""
                                pendingContent = ""
                            }, 1000)
                        }, 500)
                    }, 1500)
                }, 500)
            }, 500)
        }, 500)
    }

    private fun findChatTitle(root: AccessibilityNodeInfo): String? {
        // 尝试多种方式找到聊天标题
        val titleNode = findNodeById(root, ID_CHAT_TITLE)
            ?: findNodeByViewIdSuffix(root, "gd")
        return titleNode?.let { findTextInChildren(it) }
    }

    private fun findMessageList(root: AccessibilityNodeInfo): List<String> {
        val msgNode = findNodeById(root, ID_MSG_LIST)
            ?: findNodeByViewIdSuffix(root, "b8_")
        return msgNode?.let { collectTexts(it) } ?: emptyList()
    }

    private fun isSelfSentMessage(root: AccessibilityNodeInfo, msgText: String): Boolean {
        // 简单判断：通过检查消息气泡在左侧还是右侧
        // 右侧是自己发的 (Accessibility 可通过 layout 类型判断)
        return false // 简化版先不做
    }

    private fun findClawbotItem(root: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        return findNodeByText(root, CLAWBOT_NAME)
    }

    // ---- 辅助函数 ----

    private fun findNodeById(root: AccessibilityNodeInfo, id: String): AccessibilityNodeInfo? {
        if (root.viewIdResourceName == id) return root
        for (i in 0 until root.childCount) {
            val child = root.getChild(i) ?: continue
            val result = findNodeById(child, id)
            if (result != null) return result
        }
        return null
    }

    private fun findNodeByText(root: AccessibilityNodeInfo, text: String): AccessibilityNodeInfo? {
        if (root.text?.toString()?.contains(text, ignoreCase = true) == true) return root
        for (i in 0 until root.childCount) {
            val child = root.getChild(i) ?: continue
            val result = findNodeByText(child, text)
            if (result != null) return result
        }
        return null
    }

    private fun findNodeByViewIdSuffix(root: AccessibilityNodeInfo, suffix: String): AccessibilityNodeInfo? {
        if (root.viewIdResourceName?.endsWith(suffix) == true) return root
        for (i in 0 until root.childCount) {
            val child = root.getChild(i) ?: continue
            val result = findNodeByViewIdSuffix(child, suffix)
            if (result != null) return result
        }
        return null
    }

    private fun findTextInChildren(node: AccessibilityNodeInfo): String? {
        if (node.text != null) return node.text.toString()
        for (i in 0 until node.childCount) {
            val child = node.getChild(i) ?: continue
            val text = findTextInChildren(child)
            if (text != null) return text
        }
        return null
    }

    private fun collectTexts(node: AccessibilityNodeInfo): List<String> {
        val texts = mutableListOf<String>()
        if (node.text != null) texts.add(node.text.toString())
        for (i in 0 until node.childCount) {
            val child = node.getChild(i) ?: continue
            texts.addAll(collectTexts(child))
        }
        return texts
    }

    override fun onInterrupt() {
        Log.d(TAG, "服务中断")
    }
}
