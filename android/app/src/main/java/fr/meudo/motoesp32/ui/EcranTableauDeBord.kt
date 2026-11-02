package fr.meudo.motoesp32.ui

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole
import java.util.Locale

@Composable
fun EcranTableauDeBord(modifier: Modifier, client: MotoBleClient) {
    val t by client.telemetrie.collectAsStateWithLifecycle()
    val context = LocalContext.current

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Carte("Moto (bus CAN)") {
            val m = t.moto
            if (m == null) {
                Text("En attente des données…")
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                    GrandeValeur("${m.vitesseKmh}", "km/h")
                    GrandeValeur("${m.rpm}", "tr/min")
                    GrandeValeur(if (m.rapport == 0) "N" else "${m.rapport}", "rapport")
                }
                Ligne("Température moteur", "${m.temperatureMoteur} °C")
                Ligne("Essence", "${m.essencePct} %")
                Temoins(
                    listOf(
                        "Contact" to m.contact, "Béquille" to m.bequille, "Frein" to m.frein,
                        "Embrayage" to m.embrayage, "ABS" to m.abs, "◀ Clignotant" to m.clignotantGauche,
                        "Clignotant ▶" to m.clignotantDroit, "Codes" to m.feuxDeCroisement,
                        "Pleins phares" to m.feuxDeRoute, "Warning" to m.warning,
                    ),
                )
            }
        }

        Carte("Position GPS") {
            val g = t.gps
            if (g == null) {
                Text("En attente des données…")
            } else {
                Ligne("Signal", if (g.fix) "Position valide (${g.satellites} satellites)" else "Pas de position")
                Ligne("Coordonnées", String.format(Locale.US, "%.6f, %.6f", g.latitude, g.longitude))
                Ligne("Vitesse", String.format(Locale.FRANCE, "%.1f km/h", g.vitesseKmh))
                Ligne("Direction", "${g.cap.toInt()}° ${Protocole.cardinal(g.cap)}")
                Ligne("Altitude", "${g.altitude.toInt()} m")
                Ligne("Précision (HDOP)", String.format(Locale.FRANCE, "%.1f", g.hdop))
                if (g.fix) {
                    TextButton(onClick = {
                        val uri = Uri.parse("geo:${g.latitude},${g.longitude}?q=${g.latitude},${g.longitude}(Moto)")
                        try {
                            context.startActivity(Intent(Intent.ACTION_VIEW, uri))
                        } catch (e: ActivityNotFoundException) {
                            // Aucune application de carte installée
                        }
                    }) { Text("Voir sur la carte") }
                }
            }
        }

        Carte("Batterie du boîtier") {
            val b = t.batterie
            when {
                b == null -> Text("En attente des données…")
                !b.presente -> Text("Jauge de batterie non détectée")
                else -> {
                    Ligne("Charge", String.format(Locale.FRANCE, "%.0f %%  (%.2f V)", b.pourcentage, b.tension))
                    LinearProgressIndicator(
                        progress = { (b.pourcentage / 100.0).toFloat().coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }

        Carte("Réseau 4G") {
            val r = t.reseau
            if (r == null) {
                Text("En attente des données…")
            } else {
                Ligne("État", when (r.etat) { 2 -> "Connecté"; 1 -> "Actif, non connecté"; else -> "Non connecté" })
                Ligne("Signal", "${r.signalPct} %")
                Ligne("Data ce mois", String.format(Locale.FRANCE, "%.1f Mo", r.consommationOctets / 1048576.0))
                Ligne("Forfait utilisé", "${r.restePct} %")
            }
        }

        Carte("Accéléromètre") {
            val a = t.accelerometre
            if (a == null) {
                Text("En attente des données…")
            } else {
                Ligne("Inclinaison (roulis)", String.format(Locale.FRANCE, "%.1f°", a.roulis))
                Ligne("Assiette (tangage)", String.format(Locale.FRANCE, "%.1f°", a.tangage))
                Ligne("Force G", String.format(Locale.FRANCE, "%.2f g", a.forceG))
            }
        }

        Carte("Module") {
            val s = t.systeme
            if (s == null) {
                Text("En attente des données…")
            } else {
                Ligne("Firmware", "${s.versionFirmware} (${s.compilation})")
                Ligne("Allumé depuis", Protocole.duree(s.dureeFonctionnementS))
                Ligne("Mémoire libre", "${s.memoireLibre / 1024} Ko (PSRAM ${s.psramLibre / 1024} Ko)")
                Ligne("Partition", s.partition)
            }
        }
    }
}

@Composable
private fun GrandeValeur(valeur: String, unite: String) {
    Column {
        Text(valeur, style = MaterialTheme.typography.displaySmall, fontWeight = FontWeight.Bold)
        Text(unite, style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
fun Ligne(libelle: String, valeur: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(libelle, style = MaterialTheme.typography.bodyMedium)
        Text(valeur, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Temoins(temoins: List<Pair<String, Boolean>>) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        temoins.forEach { (nom, actif) ->
            AssistChip(
                onClick = {},
                label = { Text(nom) },
                colors = if (actif) {
                    AssistChipDefaults.assistChipColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        labelColor = MaterialTheme.colorScheme.onPrimary,
                    )
                } else {
                    AssistChipDefaults.assistChipColors()
                },
            )
        }
    }
}
