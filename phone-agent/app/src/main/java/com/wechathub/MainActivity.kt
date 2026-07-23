package com.wechathub

import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.TextView
import android.content.Intent
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var btnAccessibility: Button
    private lateinit var btnStart: Button
    private lateinit var tvStatus: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        btnAccessibility = findViewById(R.id.btnOpenAccessibility)
        btnStart = findViewById(R.id.btnStart)
        tvStatus = findViewById(R.id.tvStatus)

        btnAccessibility.setOnClickListener {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
        }

        btnStart.setOnClickListener {
            tvStatus.text = "状态: 运行中"
        }
    }

    override fun onResume() {
        super.onResume()
        val enabled = isAccessibilityEnabled()
        btnStart.isEnabled = enabled
        if (enabled) {
            tvStatus.text = "状态: 就绪（辅助功能已开启）"
        } else {
            tvStatus.text = "状态: 等待开启辅助功能"
        }
    }

    private fun isAccessibilityEnabled(): Boolean {
        val service = "${packageName}/.WeChatBridge"
        val enabledServices = Settings.Secure.getString(
            contentResolver,
            Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES
        ) ?: return false
        return enabledServices.split(":").any { it.equals(service, ignoreCase = true) }
    }
}
