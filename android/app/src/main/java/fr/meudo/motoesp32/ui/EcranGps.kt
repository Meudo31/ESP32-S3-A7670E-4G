package fr.meudo.motoesp32.ui

import android.content.Context
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.EtatGps
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.PointGps
import fr.meudo.motoesp32.ble.Protocole
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polyline
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

@Composable
fun EcranGps(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val gps = etat.gps

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        if (gps == null) {
            Carte("GPS") { Text("En attente des données…") }
            return@Column
        }

        Carte("Carte") {
            if (gps.positionValide) {
                // clipToBounds : sans lui, la carte déborde sur les lignes voisines.
                CarteOpenStreetMap(gps, etat.historiqueGps, Modifier.fillMaxWidth().height(280.dp).clipToBounds())
                Ligne("Coordonnées", String.format(Locale.US, "%.6f, %.6f", gps.latitude, gps.longitude))
            } else {
                Text("Pas encore de position. Le module doit voir le ciel (première position : quelques minutes).")
            }
            TextButton(onClick = { client.envoyerCommande("GPS") }) { Text("Actualiser la position") }
        }

        Carte("Vitesse et direction") {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                JaugeCirculaire(
                    valeur = gps.vitesseKmh.toFloat(), maximum = 200f,
                    texte = "%.0f".format(gps.vitesseKmh), unite = "km/h",
                )
                Boussole(gps.cap.toFloat(), "${gps.cap.toInt()}° ${Protocole.cardinal(gps.cap)}")
            }
            Ligne("Vitesse maximum", "%.0f km/h".format(gps.vitesseMaxKmh))
            Ligne("Altitude", "${gps.altitude.toInt()} m")
        }

        Carte("Satellites (${gps.satellites})") {
            BarresHorizontales(
                listOf(
                    Barre("GPS 🇺🇸", gps.satellitesGps.toFloat(), "${gps.satellitesGps}"),
                    Barre("GLONASS 🇷🇺", gps.satellitesGlonass.toFloat(), "${gps.satellitesGlonass}"),
                    Barre("Galileo 🇪🇺", gps.satellitesGalileo.toFloat(), "${gps.satellitesGalileo}"),
                    Barre("BeiDou 🇨🇳", gps.satellitesBeidou.toFloat(), "${gps.satellitesBeidou}"),
                ),
                maximum = 16f,
            )
            Ligne("Type de position", when (gps.typeDePosition) { 3 -> "3D (avec altitude)"; 2 -> "2D"; else -> "aucune" })
        }

        Carte("Précision : ${Protocole.qualiteGps(gps.hdop)}") {
            Text("Plus la barre est courte, plus la position est précise (moins de 2 : excellent).")
            BarresHorizontales(
                listOf(
                    Barre("Horizontale", gps.hdop.toFloat(), "HDOP %.1f".format(gps.hdop), couleurDop(gps.hdop)),
                    Barre("Verticale", gps.vdop.toFloat(), "VDOP %.1f".format(gps.vdop), couleurDop(gps.vdop)),
                    Barre("Globale", gps.pdop.toFloat(), "PDOP %.1f".format(gps.pdop), couleurDop(gps.pdop)),
                ),
                maximum = 10f,
            )
        }

        Carte("Historique") {
            Text("Vitesse")
            Courbe(etat.historiqueGps.map { it.vitesseKmh.toFloat() }, "km/h")
            Text("Altitude")
            Courbe(etat.historiqueGps.map { it.altitude.toFloat() }, "m", couleur = Color(0xFF3B82F6))
        }

        Carte("Heure des satellites") {
            Ligne("Heure UTC", if (gps.heureUtc > 0) formatUtc(gps.heureUtc) else "—")
            Ligne("Dernière mesure", if (gps.ageSecondes >= 0) "il y a ${gps.ageSecondes} s" else "—")
        }
    }
}

private fun couleurDop(dop: Double): Color = when {
    dop <= 0.0 -> Color.Gray
    dop < 2.0 -> Color(0xFF22C55E)
    dop < 5.0 -> Color(0xFFF59E0B)
    else -> Color(0xFFEF4444)
}

private fun formatUtc(secondes: Long): String {
    val format = SimpleDateFormat("dd/MM/yyyy HH:mm:ss", Locale.FRANCE)
    format.timeZone = TimeZone.getTimeZone("UTC")
    return format.format(Date(secondes * 1000))
}

/** Carte OpenStreetMap (sans clé ni compte) avec la position de la moto et sa trace récente. */
@Composable
private fun CarteOpenStreetMap(gps: EtatGps, trace: List<PointGps>, modifier: Modifier) {
    val context = LocalContext.current
    val carte = remember { creerLaCarte(context) }
    val marqueur = remember { Marker(carte).apply { title = "Moto"; setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM) } }
    val ligne = remember { Polyline(carte).apply { outlinePaint.color = android.graphics.Color.rgb(245, 158, 11); outlinePaint.strokeWidth = 8f } }

    DisposableEffect(carte) {
        carte.overlays.add(ligne)
        carte.overlays.add(marqueur)
        carte.onResume()
        onDispose { carte.onPause(); carte.onDetach() }
    }

    AndroidView(factory = { carte }, modifier = modifier, update = { vue ->
        val position = GeoPoint(gps.latitude, gps.longitude)
        marqueur.position = position
        ligne.setPoints(trace.map { GeoPoint(it.latitude, it.longitude) })
        vue.controller.setCenter(position)
        vue.invalidate()
    })
}

private fun creerLaCarte(context: Context): MapView {
    // Sans cette configuration, osmdroid essaie de ranger les tuiles dans /sdcard/osmdroid, interdit depuis
    // Android 10 : la carte reste vierge. On charge ses réglages et on range les tuiles dans le cache de l'appli.
    val configuration = Configuration.getInstance()
    configuration.load(context, context.getSharedPreferences("osmdroid", Context.MODE_PRIVATE))
    configuration.osmdroidBasePath = java.io.File(context.cacheDir, "osmdroid").apply { mkdirs() }
    configuration.osmdroidTileCache = java.io.File(configuration.osmdroidBasePath, "tuiles").apply { mkdirs() }
    // Les serveurs OpenStreetMap refusent les applications sans nom précis.
    configuration.userAgentValue = "MotoESP32/${context.packageName}"
    return MapView(context).apply {
        setTileSource(TileSourceFactory.MAPNIK)
        setMultiTouchControls(true)
        controller.setZoom(16.0)
    }
}
