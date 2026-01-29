package com.example.rowingsync.data

import retrofit2.http.*

/**
 * Retrofit API interface for ESP32 rowing monitor
 */
interface Esp32Api {
    
    @GET("api/status")
    suspend fun getStatus(): StatusResponse
    
    @GET("api/sessions")
    suspend fun getSessions(): SessionsResponse
    
    @GET("api/sessions/{id}")
    suspend fun getSession(@Path("id") id: Int): SessionDetail
    
    @POST("api/sessions/{id}/synced")
    suspend fun markSynced(@Path("id") id: Int): GenericResponse
    
    @DELETE("api/sessions/{id}")
    suspend fun deleteSession(@Path("id") id: Int): GenericResponse
    
    @POST("workout/start")
    suspend fun startWorkout(): WorkoutResponse
    
    @POST("workout/stop")
    suspend fun stopWorkout(): WorkoutResponse
    
    @GET("live")
    suspend fun getLiveData(): LiveData
    
    @GET("hr")
    suspend fun getHeartRate(): String
}
