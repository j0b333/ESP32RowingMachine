package com.example.rowingsync.data

import com.example.rowingsync.BuildConfig
import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

/**
 * Singleton API client for ESP32 communication
 */
object ApiClient {
    private var currentBaseUrl: String = "http://192.168.4.1/"  // ESP32 AP mode default
    private var retrofit: Retrofit? = null
    private var api: Esp32Api? = null
    
    private val loggingInterceptor = HttpLoggingInterceptor().apply {
        // Only log full bodies in debug builds to avoid performance/security issues
        level = if (BuildConfig.DEBUG) {
            HttpLoggingInterceptor.Level.BODY
        } else {
            HttpLoggingInterceptor.Level.BASIC
        }
    }
    
    private val okHttpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(10, TimeUnit.SECONDS)
        .addInterceptor(loggingInterceptor)
        .build()
    
    /**
     * Get API instance for the specified base URL
     * 
     * @param baseUrl The ESP32 address (e.g., "192.168.4.1" or "rowing.local")
     * @return Esp32Api instance
     */
    fun getApi(baseUrl: String = currentBaseUrl): Esp32Api {
        val normalizedUrl = normalizeUrl(baseUrl)
        
        if (api == null || currentBaseUrl != normalizedUrl) {
            currentBaseUrl = normalizedUrl
            retrofit = Retrofit.Builder()
                .baseUrl(normalizedUrl)
                .client(okHttpClient)
                .addConverterFactory(GsonConverterFactory.create())
                .build()
            api = retrofit!!.create(Esp32Api::class.java)
        }
        
        return api!!
    }
    
    /**
     * Normalize URL to ensure it has proper format
     */
    private fun normalizeUrl(url: String): String {
        var normalized = url.trim()
        
        // Add http:// if missing
        if (!normalized.startsWith("http://") && !normalized.startsWith("https://")) {
            normalized = "http://$normalized"
        }
        
        // Only add trailing slash if URL doesn't contain query parameters
        if (!normalized.endsWith("/") && !normalized.contains("?")) {
            normalized = "$normalized/"
        }
        
        return normalized
    }
    
    /**
     * Get current base URL
     */
    fun getCurrentBaseUrl(): String = currentBaseUrl
    
    /**
     * Reset the API client (useful when changing connection)
     */
    fun reset() {
        api = null
        retrofit = null
    }
}
