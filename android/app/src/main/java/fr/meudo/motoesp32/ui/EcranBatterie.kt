package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.MotoBleClient

@Composable
fun EcranBatterie(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val b = etat.batterie

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        if (b == null) {
            Carte("Batterie") { Text("En attente des données…") }
            return@Column
        }

        Carte("Batterie 18650") {
            if (!b.jaugePresente) {
                Text("Jauge de batterie MAX17048 non détectée.", color = MaterialTheme.colorScheme.error)
            } else {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceEvenly) {
                    JaugeCirculaire(
                        valeur = b.pourcentage.toFloat(), maximum = 100f,
                        texte = "%.0f %%".format(b.pourcentage), unite = "%.2f V".format(b.tension),
                        couleur = couleurSelonLeNiveau((b.pourcentage / 100).toFloat()),
                    )
                    Column {
                        Text(
                            when {
                                b.pleine -> "🔋 Pleine"
                                b.enCharge -> "⚡ En charge"
                                else -> "🔻 Se décharge"
                            },
                            style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold,
                        )
                        Text("%+.1f %% par heure".format(b.variationParHeure))
                        if (b.autonomieHeures > 0) Text("Autonomie ≈ %.0f h".format(b.autonomieHeures))
                    }
                }
                if (b.alertes != 0) Text("Alertes de la jauge : ${texteDesAlertes(b.alertes)}", color = MaterialTheme.colorScheme.error)
            }
        }

        Carte("Alimentation") {
            Ligne("Source", b.source)
            if (b.mesuresCablees) {
                Ligne("Entrée 12 V moto", "%.1f V".format(b.tension12V))
                Ligne("USB", "%.2f V".format(b.tensionUsb))
            } else {
                Text(
                    "La carte ne permet pas de savoir directement si le 12 V ou l'USB est branché : " +
                        "la source est déduite de la charge de la batterie. Pour une mesure exacte, câbler un pont " +
                        "diviseur vers une entrée analogique (voir configuration.h).",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            Ligne("Tension vue par le modem", if (b.tensionVueParLeModem > 0) "%.2f V".format(b.tensionVueParLeModem) else "—")
            Ligne("Température du processeur", "%.0f °C".format(b.temperaturePuce))
            Ligne("Version de la jauge", "0x%04X".format(b.versionJauge))
        }

        Carte("Historique") {
            Text("Tension")
            Courbe(etat.historiqueBatterie.map { it.tension.toFloat() }, "V", couleur = Color(0xFF22C55E))
            Text("Charge")
            Courbe(etat.historiqueBatterie.map { it.pourcentage.toFloat() }, "%")
        }
    }
}

/** Bits d'alerte du MAX17048 (registre STATUS). */
private fun texteDesAlertes(alertes: Int): String = buildList {
    if (alertes and 0x02 != 0) add("tension trop haute")
    if (alertes and 0x04 != 0) add("tension trop basse")
    if (alertes and 0x08 != 0) add("tension de réinitialisation")
    if (alertes and 0x10 != 0) add("charge basse")
    if (alertes and 0x20 != 0) add("variation de charge")
    if (alertes and 0x01 != 0) add("jauge redémarrée")
}.joinToString(", ")
