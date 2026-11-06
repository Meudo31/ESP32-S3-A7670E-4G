package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole
import kotlinx.coroutines.delay

/** Écran « Module » : processeur, mémoires, tâches FreeRTOS, carte SD, version de la carte. */
@Composable
fun EcranSysteme(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    var confirmerRedemarrage by remember { mutableStateOf(false) }
    var commandeLibre by remember { mutableStateOf("") }

    // La liste des tâches n'est envoyée que sur demande : on la redemande toutes les 10 s sur cet écran.
    LaunchedEffect(Unit) {
        while (true) {
            client.envoyerCommande("SYSTEME")
            delay(10_000)
        }
    }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        etat.carte?.let { c ->
            Carte("Carte Waveshare ${c.version}") {
                Text("Détection : ${c.methodeDeDetection}", style = MaterialTheme.typography.bodySmall)
                Ligne("Caméra", c.camera)
                Ligne("Heure donnée par", c.sourceDeLHeure)
            }
        }

        etat.liaison?.let { l ->
            Carte("Liaison Bluetooth") {
                Ligne("Signal reçu par le module", if (l.signalDbm != 0) "${l.signalDbm} dBm (${qualiteDuSignal(l.signalDbm)})" else "—")
                Ligne("Intervalle", "%.1f ms".format(l.intervalleMs))
                Ligne("Coupure après", "%.1f s sans échange".format(l.delaiDeCoupureS))
                Ligne("Déconnexions depuis le démarrage", "${l.nombreDeDeconnexions}")
                if (l.historique.isNotEmpty()) {
                    Text("Dernières coupures :", style = MaterialTheme.typography.labelMedium)
                    l.historique.forEach { Text(it, style = MaterialTheme.typography.bodySmall) }
                }
            }
        }

        etat.systeme?.let { s ->
            Carte("Processeur") {
                Ligne("Puce", "${s.puce} rév. ${s.revision}")
                Ligne("Cœurs", "${s.coeurs} à ${s.frequenceMhz} MHz")
                Ligne("Température", "%.0f °C".format(s.temperature))
                Ligne("Allumé depuis", Protocole.duree(s.dureeFonctionnementS))
                Ligne("Dernier redémarrage", s.raisonRedemarrage)
                Ligne("Nombre de démarrages", "${s.nombreDeDemarrages}")
                Ligne("Adresse MAC", s.adresseMac)
            }
            Carte("Firmware") {
                Ligne("Version", s.versionFirmware)
                Ligne("Compilé le", s.compilation)
                Ligne("Partition active", s.partition)
                Ligne("SDK Espressif", s.versionSdk)
                Ligne("Flash", "${Protocole.taille(s.tailleFlash)} à ${s.frequenceFlashMhz} MHz")
                BarresHorizontales(
                    listOf(Barre("Programme", s.tailleProgramme.toFloat(),
                        "${Protocole.taille(s.tailleProgramme)} / ${Protocole.taille(s.tailleProgramme + s.placeLibrePourProgramme)}")),
                    maximum = (s.tailleProgramme + s.placeLibrePourProgramme).toFloat(),
                )
            }
        }

        etat.memoires?.let { m ->
            Carte("Mémoires") {
                val ramUtilisee = m.ramTotale - m.ramLibre
                val psramUtilisee = m.psramTotale - m.psramLibre
                BarresHorizontales(
                    listOf(
                        Barre("RAM", ramUtilisee.toFloat() / maxOf(m.ramTotale, 1L), "${Protocole.taille(ramUtilisee)} / ${Protocole.taille(m.ramTotale)}",
                            couleurSelonLeNiveau(m.ramLibre.toFloat() / maxOf(m.ramTotale, 1L))),
                        Barre("PSRAM", psramUtilisee.toFloat() / maxOf(m.psramTotale, 1L), "${Protocole.taille(psramUtilisee)} / ${Protocole.taille(m.psramTotale)}",
                            couleurSelonLeNiveau(m.psramLibre.toFloat() / maxOf(m.psramTotale, 1L))),
                    ),
                    maximum = 1f,
                )
                Ligne("RAM libre (minimum atteint)", Protocole.taille(m.ramMinimum))
                Ligne("Plus grand bloc de RAM", Protocole.taille(m.ramPlusGrandBloc))
                Ligne("PSRAM libre (minimum atteint)", Protocole.taille(m.psramMinimum))
            }
        }

        etat.carte?.let { c ->
            Carte("Carte SD") {
                if (!c.carteSdPresente) Text("Pas de carte SD.", color = MaterialTheme.colorScheme.error)
                else {
                    BarresHorizontales(
                        listOf(Barre(c.typeCarteSd, c.espaceUtiliseCarteSd.toFloat(),
                            "${Protocole.taille(c.espaceUtiliseCarteSd)} / ${Protocole.taille(c.tailleCarteSd)}")),
                        maximum = c.tailleCarteSd.toFloat(),
                    )
                }
            }
        }

        if (etat.taches.isNotEmpty()) {
            Carte("Tâches FreeRTOS (${etat.taches.size})") {
                Text("Pile utilisée par chaque tâche (rouge = presque pleine).", style = MaterialTheme.typography.bodySmall)
                BarresHorizontales(
                    etat.taches.map { t ->
                        val utilisee = t.tailleDeLaPile - t.pileRestante
                        Barre(
                            "${t.nom} (c${t.coeur})", utilisee.toFloat() / maxOf(t.tailleDeLaPile, 1L),
                            "${t.pileRestante} o libres",
                            couleurSelonLeNiveau(t.pileRestante.toFloat() / maxOf(t.tailleDeLaPile, 1L) * 2),
                        )
                    },
                    maximum = 1f,
                )
            }
        }

        Carte("Actions") {
            Button(
                onClick = { confirmerRedemarrage = true },
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.RestartAlt, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Redémarrer le module")
            }
            Text("Commande manuelle (pour tester une commande du firmware) :", style = MaterialTheme.typography.bodySmall)
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = commandeLibre, onValueChange = { commandeLibre = it.take(200) },
                    singleLine = true, modifier = Modifier.weight(1f), placeholder = { Text("ex. PING") },
                )
                TextButton(onClick = { client.envoyerCommande(commandeLibre.trim()); commandeLibre = "" }, enabled = commandeLibre.isNotBlank()) {
                    Text("Envoyer")
                }
            }
        }
    }

    if (confirmerRedemarrage) {
        AlertDialog(
            onDismissRequest = { confirmerRedemarrage = false },
            title = { Text("Redémarrer le module ?") },
            text = { Text("La connexion Bluetooth sera coupée pendant quelques secondes.") },
            confirmButton = { TextButton(onClick = { confirmerRedemarrage = false; client.envoyerCommande("REBOOT") }) { Text("Redémarrer") } },
            dismissButton = { TextButton(onClick = { confirmerRedemarrage = false }) { Text("Annuler") } },
        )
    }
}

private fun qualiteDuSignal(dbm: Int): String = when {
    dbm >= -60 -> "excellent"
    dbm >= -70 -> "bon"
    dbm >= -80 -> "moyen"
    dbm >= -90 -> "faible"
    else -> "très faible"
}
