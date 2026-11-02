package fr.meudo.motoesp32.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Ambre = Color(0xFFF59E0B)
private val AmbreFonce = Color(0xFFB45309)

private val Sombre = darkColorScheme(
    primary = Ambre,
    onPrimary = Color(0xFF1F1300),
    secondary = Color(0xFF93C5FD),
    background = Color(0xFF111418),
    surface = Color(0xFF111418),
    surfaceVariant = Color(0xFF1E242B),
)

private val Clair = lightColorScheme(
    primary = AmbreFonce,
    onPrimary = Color.White,
    secondary = Color(0xFF1D4ED8),
)

@Composable
fun ThemeMoto(contenu: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Sombre else Clair, content = contenu)
}
