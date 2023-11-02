package io.pag.tgfxdemo

import android.os.Bundle
import androidx.activity.ComponentActivity

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<TGFXView>(R.id.tgfx_demo_view)
        view.setOnClickListener {
            view.draw()
        }
    }
}
