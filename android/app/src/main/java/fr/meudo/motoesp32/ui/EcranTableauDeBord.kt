package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole

/** Vue d'ensemble : un résumé de chaque partie, avec un lien vers l'écran détaillé. */
@Composable
fun EcranTableauDeBord(modifier: Modifier, client: MotoBleClient, ouvrir: (Ecran) -> Unit) {
    val etat by client.etatModule.collectAsStateWithLifecycle()

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            val gps = etat.gps
            JaugeCirculaire(
                valeur = (gps?.vitesseKmh ?: 0.0).toFloat(), maximum = 200f,
                texte = if (gps?.positionValide == true) "%.0f".format(gps.vitesseKmh) else "—", unite = "km/h",
            )
            val b = etat.batterie
            JaugeCirculaire(
                valeur = (b?.pourcentage ?: 0.0).toFloat(), maximum = 100f,
                texte = if (b?.jaugePresente == true) "%.0f %%".format(b.pourcentage) else "—",
                unite = if (b?.enCharge == true) "⚡ en charge" else "batterie",
                couleur = couleurSelonLeNiveau(((b?.pourcentage ?: 0.0) / 100).toFloat()),
            )
        }

        Carte("Position") {
            val g = etat.gps
            Text(
                when {
                    g == null -> "En attente des données…"
                    g.positionValide -> "%.5f, %.5f · %d satellites · %s".format(g.latitude, g.longitude, g.satellites, Protocole.qualiteGps(g.hdop))
                    else -> "Pas de position (${g.satellites} satellites)"
                },
            )
            TextButton(onClick = { ouvrir(Ecran.GPS) }) { Text("Carte et détails") }
        }

        Carte("Réseau 4G") {
            val r = etat.reseau
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                BarresDeSignal(r?.signalPct ?: 0)
                Text(if (r == null) "En attente…" else "${r.operateur.ifEmpty { "—" }} ${r.technologie} · ${Protocole.nomInscription(r.inscription)}")
            }
            TextButton(onClick = { ouvrir(Ecran.TELEPHONE) }) { Text("SMS, appels et réseau") }
        }

        Carte("Lumières") {
            val n = etat.neopixels
            Text(if (n == null) "En attente…" else if (n.allumee) "Animation : ${n.nom}" else "Bande éteinte")
            TextButton(onClick = { ouvrir(Ecran.LUMIERES) }) { Text("Changer d'animation") }
        }

        Carte("Module") {
            val s = etat.systeme
            val c = etat.carte
            if (s != null) {
                Ligne("Firmware", s.versionFirmware)
                Ligne("Carte", c?.version ?: s.versionCarte)
                Ligne("Allumé depuis", Protocole.duree(s.dureeFonctionnementS))
            }
            if (c != null) Ligne("Carte SD", if (c.carteSdPresente) "${Protocole.taille(c.espaceUtiliseCarteSd)} / ${Protocole.taille(c.tailleCarteSd)}" else "absente")
            etat.memoires?.let { Ligne("RAM libre", Protocole.taille(it.ramLibre)) }
            TextButton(onClick = { ouvrir(Ecran.SYSTEME) }) { Text("Mémoires, processeur, tâches") }
        }
    }
}
