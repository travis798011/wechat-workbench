package com.wechathub

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.widget.Button
import android.widget.TextView
import android.content.Intent
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var tvStatus: TextView
    private lateinit var tvLog: TextView
    private val handler = Handler(Looper.getMainLooper())
    private var previousSize = 0

    private val poller = object : Runnable {
        override fun run() {
            val logs = WeChatBridge.capturedMessages
            if (logs.size > previousSize) {
                tvLog.text = logs.joinToString("\n\n")
                previousSize = logs.size
            }
            handler.postDelayed(this, 500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val btnAccessibility = findViewById<Button>(R.id.btnOpenAccessibility)
        tvStatus = findViewById(R.id.tvStatus)
        tvLog = findViewById(R.id.tvLog)

        btnAccessibility.setOnClickListener {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
        }

        handler.post(poller)
    }

    override fun onResume() {
        super.onResume()
        if (isAccessibilityEnabled()) {
            tvStatus.text = "状态: 就绪（辅助功能已开启）"
        } else {
            tvStatus.text = "状态: 等待开启辅助功能"
        }
    }

    private fun isAccessibilityEnabled(): Boolean {
        val service = "${packageName}/.WeChatBridge"
        val serviceFull = "${packageName}/com.wechathub.WeChatBridge"
        val enabledServices = Settings.Secure.getString(
            contentResolver,
            Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES
        ) ?: return false
        return enabledServices.split(":").any {
            it.equals(service, ignoreCase = true) || it.equals(serviceFull, ignoreCase = true)
        }
    }
}
