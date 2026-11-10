package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.EtatCan
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole
import fr.meudo.motoesp32.ble.ValeursCan

/** Bus CAN de la moto : tableau de bord, état de l'écoute, codes défaut et liste des trames vues. */
@Composable
fun EcranCanBus(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val can = etat.canBus

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // Tableau de bord toujours affiché : sans données, voyants éteints et valeurs « — »
        TableauDeBordCan(can?.valeurs ?: ValeursCan(), actif = can?.etat == "actif")

        if (can == null) {
            Carte("Bus CAN") { Text("En attente des données… (firmware 0.5.0 ou plus récent nécessaire)") }
            return@Column
        }

        CarteEtatDuBus(can, client)
        CarteDefauts(can)

        if (etat.historiqueCan.size > 2) {
            Carte("Historique") {
                Text("Régime")
                Courbe(etat.historiqueCan.map { it.regime.toFloat() }, "tr/min", couleur = Color(0xFFF97316))
                Text("Vitesse")
                Courbe(etat.historiqueCan.map { it.vitesse.toFloat() }, "km/h")
            }
        }

        CarteTrames(etat.tramesCan, can.nombreDIdentifiants, client)

        Text(
            "Les décodages viennent d'une Indian Springfield 2017 et de la norme J1939 : à vérifier sur la Scout. " +
                "Le module est en écoute seule, il n'envoie jamais rien sur le bus de la moto.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@Composable
private fun CarteEtatDuBus(can: EtatCan, client: MotoBleClient) {
    Carte("Écoute du bus") {
        val (texte, couleur) = when (can.etat) {
            "actif" -> "🟢 Trames reçues : ${can.tramesParSeconde} par seconde" to Color(0xFF22C55E)
            "recherche" -> "🔎 Recherche de la vitesse du bus…" to MaterialTheme.colorScheme.onSurface
            "silence" -> "🌙 Bus silencieux (moto coupée ?)" to MaterialTheme.colorScheme.onSurface
            else -> "⚪ Aucune trame : moto coupée ou transceiver non branché" to MaterialTheme.colorScheme.onSurface
        }
        Text(texte, color = couleur, style = MaterialTheme.typography.titleSmall)
        if (can.probleme.isNotEmpty() && can.etat != "actif") Text(can.probleme, color = MaterialTheme.colorScheme.error)
        Ligne("Vitesse du bus", if (can.vitesseKbits > 0) "${can.vitesseKbits} kbit/s" else "—")
        Ligne("Trames reçues", "${can.totalDesTrames}")
        Ligne("Identifiants différents", "${can.nombreDIdentifiants}")
        Ligne("Erreurs du bus", "${can.erreurs}")
        if (can.ageSecondes >= 0) Ligne("Dernière trame", "il y a ${can.ageSecondes} s")
        Ligne("Branchement", "TX GPIO ${can.brocheTx} · RX GPIO ${can.brocheRx}")

        Text("Vitesse du bus", style = MaterialTheme.typography.labelLarge)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0 to "Auto", 125 to "125", 250 to "250", 500 to "500").forEach { (kbits, nom) ->
                FilterChip(
                    selected = can.vitesseReglee == kbits,
                    onClick = { client.envoyerCommande("CAN_VITESSE", kbits.toString()) },
                    label = { Text(nom) },
                )
            }
        }

        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Enregistrer les trames sur la carte SD")
                Text(
                    if (can.fichier.isNotEmpty()) can.fichier else "Dossier /can de la carte SD",
                    style = MaterialTheme.typography.bodySmall,
                )
                if (can.tramesPerdues > 0) Text("${can.tramesPerdues} trame(s) non enregistrée(s) (carte trop lente)",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
            }
            Switch(checked = can.enregistrement, onCheckedChange = { client.envoyerCommande("CAN_ENREGISTRER", if (it) "1" else "0") })
        }
    }
}

@Composable
private fun CarteDefauts(can: EtatCan) {
    val lampes = can.valeurs.lampes
    if (can.codesDefaut.isEmpty() && (lampes == null || lampes == 0)) return
    Carte("Codes défaut (DM1)") {
        if (lampes != null && lampes != 0) {
            val noms = buildList {
                if (lampes and 1 != 0) add("moteur (MIL)")
                if (lampes and 2 != 0) add("stop")
                if (lampes and 4 != 0) add("alerte")
                if (lampes and 8 != 0) add("protection")
            }
            Text("Voyants allumés : ${noms.joinToString(", ")}", color = MaterialTheme.colorScheme.error)
        }
        can.codesDefaut.forEach { c ->
            Ligne("SPN ${c.spn} · FMI ${c.fmi}", "${nomDuModule(c.source)} · ×${c.occurrences}")
        }
    }
}

@Composable
private fun CarteTrames(trames: List<fr.meudo.motoesp32.ble.TrameCan>, nombreVues: Int, client: MotoBleClient) {
    Carte("Trames vues ($nombreVues)") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { client.envoyerCommande("CAN_TRAMES") }) { Text("Actualiser") }
            OutlinedButton(onClick = { client.envoyerCommande("CAN_EFFACER") }) { Text("Effacer") }
        }
        if (trames.isEmpty()) Text("Appuyez sur « Actualiser » pour voir la liste.", style = MaterialTheme.typography.bodySmall)
        trames.sortedBy { it.identifiant }.forEach { t ->
            val details = if (t.etendue) {
                Protocole.pgnEtSource(t.identifiant)?.let { (pgn, sa) -> "PGN $pgn · ${nomDuModule(sa)}" } ?: ""
            } else "11 bits"
            Column {
                Text("${t.identifiant}  ${t.donnees}", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                Text("$details · ×${t.compteur} · il y a ${t.ageSecondes} s", style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

/** Adresses J1939 vues sur les Indian. */
private fun nomDuModule(source: Int): String = when (source) {
    0 -> "moteur"
    11 -> "ABS"
    23 -> "compteur"
    39 -> "gestion"
    else -> "module $source"
}
