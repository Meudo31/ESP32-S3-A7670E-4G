package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Reply
import androidx.compose.material.icons.filled.Send
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.EtatAppel
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole

/** Écran « Téléphone » : trois onglets SMS, Appels et Réseau 4G. */
@Composable
fun EcranTelephone(modifier: Modifier, client: MotoBleClient) {
    var onglet by rememberSaveable { mutableIntStateOf(0) }
    Column(modifier) {
        TabRow(selectedTabIndex = onglet) {
            listOf("SMS", "Appels", "Réseau 4G").forEachIndexed { index, titre ->
                Tab(selected = onglet == index, onClick = { onglet = index }, text = { Text(titre) })
            }
        }
        when (onglet) {
            0 -> OngletSms(client)
            1 -> OngletAppels(client)
            else -> OngletReseau(client)
        }
    }
}

// ─────────────────────────────── SMS ───────────────────────────────

@Composable
private fun OngletSms(client: MotoBleClient) {
    val listeSms by client.sms.collectAsStateWithLifecycle()
    var numero by rememberSaveable { mutableStateOf("") }
    var texte by rememberSaveable { mutableStateOf("") }

    LaunchedEffect(Unit) { client.envoyerCommande("SMS_LISTE") }

    LazyColumn(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        item {
            Carte("Nouveau SMS") {
                OutlinedTextField(
                    value = numero, onValueChange = { numero = it.filter { c -> c.isDigit() || c == '+' } },
                    label = { Text("Numéro") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
                )
                OutlinedTextField(
                    value = texte, onValueChange = { texte = it.take(450) },
                    label = { Text("Message") }, modifier = Modifier.fillMaxWidth(), minLines = 2,
                )
                Text(compteurDeSms(texte), style = MaterialTheme.typography.bodySmall)
                Button(
                    onClick = {
                        client.envoyerCommande("SMS_ENVOYER", numero, texte.replace("|", "/"))
                        texte = ""
                    },
                    enabled = numero.length >= 3 && texte.isNotBlank(),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Icon(Icons.Filled.Send, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text("Envoyer")
                }
            }
        }
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Messages de la carte SIM (${listeSms.size})", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                IconButton(onClick = { client.envoyerCommande("SMS_LISTE") }) { Icon(Icons.Filled.Refresh, contentDescription = "Actualiser") }
            }
        }
        if (listeSms.isEmpty()) item { Text("Aucun SMS.") }
        items(listeSms.sortedByDescending { it.date }, key = { it.numeroDansLaSim }) { sms ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(sms.expediteur + if (!sms.lu) "  •  nouveau" else "", style = MaterialTheme.typography.titleSmall)
                            Text(sms.date, style = MaterialTheme.typography.bodySmall)
                        }
                        IconButton(onClick = { numero = sms.expediteur }) { Icon(Icons.Filled.Reply, contentDescription = "Répondre") }
                        IconButton(onClick = {
                            client.envoyerCommande("SMS_SUPPRIMER ${sms.numeroDansLaSim}")
                            client.envoyerCommande("SMS_LISTE")
                        }) { Icon(Icons.Filled.Delete, contentDescription = "Supprimer") }
                    }
                    Text(sms.texte)
                }
            }
        }
    }
}

/** « 45 caractères · 1 SMS » : 160 caractères par SMS sans accent, 70 avec. */
private fun compteurDeSms(texte: String): String {
    val simple = texte.all { it.code < 128 && it !in "[]{}\\^~|`" }
    val parSms = if (simple) 160 else 70
    val nombreDeSms = if (texte.isEmpty()) 0 else (texte.length + parSms - 1) / parSms
    return "${texte.length} caractères · $nombreDeSms SMS" + if (!simple) " (accents : 70 caractères par SMS)" else ""
}

// ─────────────────────────────── Appels ───────────────────────────────

@Composable
private fun OngletAppels(client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val appel = etat.appel
    var numero by rememberSaveable { mutableStateOf("") }

    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (etat.sim?.voixDisponible == false) {
            Carte("Appels peut-être indisponibles") {
                Text(
                    "Ce modem (${etat.sim?.modele}) a refusé une commande audio : il n'a peut-être pas de fonction voix. " +
                        "Tu peux quand même essayer d'appeler : le journal affichera la réponse exacte du modem.",
                    color = MaterialTheme.colorScheme.error,
                )
            }
        }
        Carte("Appel en cours") {
            when (appel?.etat ?: EtatAppel.Etat.AUCUN) {
                EtatAppel.Etat.AUCUN -> Text("Aucun appel.")
                EtatAppel.Etat.SONNERIE -> Text("📞 Appel entrant : ${appel?.numero}", style = MaterialTheme.typography.titleMedium)
                EtatAppel.Etat.NUMEROTATION -> Text("Appel vers ${appel?.numero}…", style = MaterialTheme.typography.titleMedium)
                EtatAppel.Etat.EN_COMMUNICATION -> Text(
                    "En communication avec ${appel?.numero} (${Protocole.duree(appel?.dureeSecondes ?: 0)})",
                    style = MaterialTheme.typography.titleMedium,
                )
            }
            BoutonsDAppel(client, appel?.etat ?: EtatAppel.Etat.AUCUN)
            if (appel?.etat == EtatAppel.Etat.EN_COMMUNICATION) {
                val muet = appel.microCoupe
                OutlinedButton(onClick = { client.envoyerCommande("MICRO_MUET", if (muet) "0" else "1") }) {
                    Text(if (muet) "🎙️ Rouvrir le micro" else "🔇 Couper le micro")
                }
                ClavierDTMF(client)
            }
        }
        CarteDuMicro(client, appel)
        Carte("Appeler") {
            OutlinedTextField(
                value = numero, onValueChange = { numero = it.filter { c -> c.isDigit() || c == '+' } },
                label = { Text("Numéro") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
            )
            Button(onClick = { client.envoyerCommande("APPELER $numero") }, enabled = numero.length >= 3, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Filled.Call, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Appeler")
            }
        }
    }
}

/** Clavier pour envoyer des touches au correspondant (serveur vocal, messagerie). */
@Composable
private fun ClavierDTMF(client: MotoBleClient) {
    val lignes = listOf("123", "456", "789", "*0#")
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        for (ligne in lignes) {
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (touche in ligne) {
                    FilledTonalButton(onClick = { client.envoyerCommande("DTMF", touche.toString()) }) { Text(touche.toString()) }
                }
            }
        }
    }
}

/** Réglages du micro de la carte (relié au modem) et décrochage automatique. */
@Composable
private fun CarteDuMicro(client: MotoBleClient, appel: EtatAppel?) {
    var gain by remember(appel?.gainMicro) { mutableFloatStateOf((appel?.gainMicro ?: 3).toFloat()) }
    Carte("Micro") {
        Text(
            "Le micro est soudé sur la carte (petit boîtier métal « 6186 ») et relié au modem : " +
                "le correspondant vous entend sans rien brancher. Votre voix à vous sort sur le haut-parleur branché sur SPK.",
            style = MaterialTheme.typography.bodySmall,
        )
        Text("Sensibilité : ${gain.toInt()} / 8")
        Slider(
            value = gain, onValueChange = { gain = it }, valueRange = 0f..8f, steps = 7,
            onValueChangeFinished = { client.envoyerCommande("MICRO_GAIN", gain.toInt().toString()) },
        )
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Switch(
                checked = appel?.decrochageAuto == true,
                onCheckedChange = { client.envoyerCommande("APPEL_AUTO", if (it) "1" else "0") },
            )
            Text("Décrocher tout seul les appels du numéro des alertes", modifier = Modifier.weight(1f))
        }
        Text(
            "Par SMS depuis le numéro des alertes : « Appelle » fait rappeler ce numéro par le module.",
            style = MaterialTheme.typography.bodySmall,
        )
        OutlinedButton(onClick = { client.envoyerCommande("MICRO_TEST") }) { Text("Tester le micro (lecture des réglages du modem)") }
    }
}

/** Boutons Décrocher / Raccrocher, selon l'état de l'appel. Aussi utilisés dans la fenêtre d'appel entrant. */
@Composable
fun BoutonsDAppel(client: MotoBleClient, etat: EtatAppel.Etat) {
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        if (etat == EtatAppel.Etat.SONNERIE) {
            Button(
                onClick = { client.envoyerCommande("DECROCHER") },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF16A34A)),
            ) {
                Icon(Icons.Filled.Call, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Décrocher")
            }
        }
        if (etat != EtatAppel.Etat.AUCUN) {
            Button(
                onClick = { client.envoyerCommande("RACCROCHER") },
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
            ) {
                Icon(Icons.Filled.CallEnd, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Raccrocher")
            }
        }
    }
}

// ─────────────────────────────── Réseau 4G ───────────────────────────────

@Composable
private fun OngletReseau(client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val reseau = etat.reseau
    val sim = etat.sim

    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Carte("Signal") {
            if (reseau == null) {
                Text("En attente des données…")
            } else {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    BarresDeSignal(reseau.signalPct)
                    Column {
                        Text("${reseau.operateur.ifEmpty { "Aucun opérateur" }} · ${reseau.technologie}", style = MaterialTheme.typography.titleMedium)
                        Text("${reseau.signalPct} %  (${reseau.signalDbm} dBm)")
                    }
                }
                Ligne("Inscription", Protocole.nomInscription(reseau.inscription))
                if (reseau.technologie == "LTE") {
                    BarresHorizontales(
                        listOf(
                            // RSRP : -80 dBm excellent, -120 dBm très faible
                            Barre("RSRP", (reseau.rsrp + 140).toFloat(), "%.0f dBm".format(reseau.rsrp), couleurSelonLeNiveau(((reseau.rsrp + 120) / 40).toFloat())),
                            // RSRQ : -3 dB excellent, -20 dB mauvais
                            Barre("RSRQ", (reseau.rsrq + 20).toFloat(), "%.1f dB".format(reseau.rsrq), couleurSelonLeNiveau(((reseau.rsrq + 20) / 17).toFloat())),
                            // SNR : > 20 dB excellent, < 0 dB mauvais
                            Barre("SNR", (reseau.snr + 10).toFloat(), "%.0f dB".format(reseau.snr), couleurSelonLeNiveau(((reseau.snr) / 20).toFloat())),
                        ),
                        maximum = 70f,
                    )
                }
                Text("Historique du signal (%)")
                Courbe(etat.historiqueSignal.map { it.signalPct.toFloat() }, "%")
            }
            FilledTonalButton(onClick = { client.envoyerCommande("RESEAU") }) { Text("Actualiser") }
        }
        if (reseau != null) {
            Carte("Cellule") {
                Ligne("Pays et opérateur (MCC-MNC)", reseau.mccMnc)
                Ligne("Zone (TAC)", reseau.zone)
                Ligne("Cellule", reseau.cellule)
                Ligne("Bande", reseau.bande)
                Ligne("Canal (EARFCN)", "${reseau.canal}")
                Ligne("RSSI", "%.1f dBm".format(reseau.rssi))
            }
            Carte("Données mobiles") {
                Ligne("Point d'accès (APN)", reseau.pointDAcces.ifEmpty { "—" })
                Ligne("Adresse IP", reseau.adresseIp.ifEmpty { "pas de connexion" })
            }
        }
        if (sim != null) {
            CarteDesAlertes(client, sim)
            Carte("Carte SIM") {
                Ligne("État", sim.etat)
                Ligne("Numéro", sim.numero.ifEmpty { "inconnu de la SIM" })
                Ligne("ICCID", sim.iccid)
                Ligne("IMSI", sim.imsi)
            }
            Carte("Modem") {
                Ligne("Modèle", sim.modele)
                Ligne("Logiciel", sim.versionLogiciel)
                Ligne("IMEI", sim.imei)
                Ligne("Voix / haut-parleur", if (sim.voixDisponible) "oui" else "non")
            }
        }
    }
}

/** Alertes SMS automatiques : démarrage, position trouvée, déplacement de plus de 5 km. */
@Composable
private fun CarteDesAlertes(client: MotoBleClient, sim: fr.meudo.motoesp32.ble.EtatSim) {
    var numero by rememberSaveable(sim.numeroDesAlertes) { mutableStateOf(sim.numeroDesAlertes) }
    Carte("Alertes SMS") {
        Text(
            "Le module envoie un SMS à ce numéro : à l'allumage (batterie, réseau), quand le GPS trouve une position " +
                "(lien Google Maps), puis à chaque déplacement de plus de 5 km.",
            style = MaterialTheme.typography.bodySmall,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                Text("Envoi automatique de SMS")
                Text(
                    if (sim.alertesActives) "Activé" else "Arrêté : aucun SMS automatique ne part",
                    style = MaterialTheme.typography.bodySmall,
                    color = if (sim.alertesActives) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.error,
                )
            }
            androidx.compose.material3.Switch(
                checked = sim.alertesActives,
                onCheckedChange = { client.envoyerCommande("ALERTES ${if (it) 1 else 0}") },
            )
        }
        // Chaque genre d'alerte se coupe à part (grisé quand l'interrupteur général est coupé).
        LigneDAlerte(client, "À l'allumage", "demarrage", sim.alerteDemarrage, sim.alertesActives)
        LigneDAlerte(client, "Position GPS trouvée", "position", sim.alertePosition, sim.alertesActives)
        LigneDAlerte(client, "Déplacement de plus de 5 km", "deplacement", sim.alerteDeplacement, sim.alertesActives)
        Text(
            "Depuis ce numéro, un SMS « Stop » coupe les alertes et « Marche » les remet (le module répond par SMS).",
            style = MaterialTheme.typography.bodySmall,
        )
        OutlinedTextField(
            value = numero, onValueChange = { numero = it.filter { c -> c.isDigit() || c == '+' }.take(20) },
            label = { Text("Numéro (+33…)") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Phone),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { client.envoyerCommande("ALERTES_NUMERO $numero") }, enabled = numero != sim.numeroDesAlertes) {
                Text("Enregistrer")
            }
            FilledTonalButton(onClick = { client.envoyerCommande("ALERTES_TEST") }, enabled = sim.numeroDesAlertes.isNotEmpty()) {
                Text("Envoyer un SMS de test")
            }
        }
    }
}

@Composable
private fun LigneDAlerte(client: MotoBleClient, titre: String, type: String, active: Boolean, general: Boolean) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(start = 16.dp)) {
        Text(
            titre, modifier = Modifier.weight(1f),
            color = if (general) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
        )
        androidx.compose.material3.Switch(
            checked = active, enabled = general,
            onCheckedChange = { client.envoyerCommande("ALERTES_TYPE $type|${if (it) 1 else 0}") },
        )
    }
}
