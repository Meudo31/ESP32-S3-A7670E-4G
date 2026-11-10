package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.EtatSynchro
import fr.meudo.motoesp32.ble.MotoBleClient
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Écran « Synchro à la maison » : quand la moto est à portée du Wi-Fi de la maison, le module y déverse
 * ses journaux et sa trace GPS (base Supabase), ses photos (Google Photos) et ses trames CAN (Google Drive).
 * Les mots de passe et les clés partent vers le module mais n'en reviennent jamais : un champ laissé vide
 * garde la valeur déjà enregistrée.
 */
@Composable
fun EcranSynchro(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { client.envoyerCommande("SYNCHRO_ETAT") }
    val s = etat.synchro

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        if (s == null) {
            Carte("Synchronisation") { Text("En attente du module (firmware 0.6.0 ou plus récent)…") }
            return@Column
        }
        CarteEtat(client, s)
        CarteWifi(client, s)
        CarteBase(client, s)
        CarteGoogle(client, s)
        CarteTramesCan(client, s)
    }
}

@Composable
private fun CarteEtat(client: MotoBleClient, s: EtatSynchro) {
    Carte("État") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Synchronisation automatique")
                Text(
                    "Le module cherche le Wi-Fi toutes les minutes à l'arrêt, puis envoie au plus toutes les 15 minutes.",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            Switch(checked = s.active, onCheckedChange = { client.envoyerCommande("SYNCHRO ${if (it) 1 else 0}") })
        }
        if (s.enCours) LinearProgressIndicator(Modifier.fillMaxWidth())
        Ligne("En ce moment", s.etat.ifEmpty { "—" })
        Ligne(
            "Dernière synchro réussie",
            if (s.derniereSynchroUtc > 0) SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE).format(Date(s.derniereSynchroUtc * 1000)) else "jamais",
        )
        Ligne("Lignes de journal", s.lignesDeJournal.toString())
        Ligne("Positions GPS", s.positions.toString())
        Ligne("Fichiers CAN (Drive)", s.fichiersCan.toString())
        if (s.tramesCan > 0) Ligne("Trames CAN (base)", s.tramesCan.toString())
        Ligne("Photos", s.photos.toString())
        if (s.derniereErreur.isNotEmpty()) {
            Text("⚠️ ${s.derniereErreur}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
        }
        Button(onClick = { client.envoyerCommande("SYNCHRO_MAINTENANT") }, enabled = !s.enCours && s.wifiNom.isNotEmpty()) {
            Text("Synchroniser maintenant")
        }
    }
}

@Composable
private fun CarteWifi(client: MotoBleClient, s: EtatSynchro) {
    var nom by rememberSaveable(s.wifiNom) { mutableStateOf(s.wifiNom) }
    var motDePasse by rememberSaveable { mutableStateOf("") }
    Carte("Wi-Fi de la maison") {
        OutlinedTextField(nom, { nom = it.take(32) }, label = { Text("Nom du réseau (SSID)") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        ChampSecret(motDePasse, { motDePasse = it.take(63) }, if (s.wifiMotDePasseRegle) "Mot de passe (enregistré, vide = inchangé)" else "Mot de passe")
        Button(
            onClick = { client.envoyerCommande("SYNCHRO_WIFI", nom.trim(), motDePasse); motDePasse = "" },
            enabled = nom.isNotBlank() && (nom.trim() != s.wifiNom || motDePasse.isNotEmpty()),
        ) { Text("Enregistrer") }
    }
}

@Composable
private fun CarteBase(client: MotoBleClient, s: EtatSynchro) {
    var adresse by rememberSaveable(s.adresseApi) { mutableStateOf(s.adresseApi) }
    var cle by rememberSaveable { mutableStateOf("") }
    Carte("Base de données (journaux et GPS)") {
        Text(
            "Supabase : https://<projet>.supabase.co/rest/v1 et la clé « publishable ». " +
                "Serveur maison : http://192.168.1.x:3000. Voir docs/synchro_maison.md.",
            style = MaterialTheme.typography.bodySmall,
        )
        OutlinedTextField(
            adresse, { adresse = it.trim().take(200) }, label = { Text("Adresse de l'API") }, singleLine = true,
            modifier = Modifier.fillMaxWidth(), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
        )
        ChampSecret(cle, { cle = it.trim().take(400) }, if (s.cleReglee) "Clé (enregistrée, vide = inchangée)" else "Clé de l'API")
        Button(
            onClick = { client.envoyerCommande("SYNCHRO_SERVEUR", adresse, cle); cle = "" },
            enabled = adresse != s.adresseApi || cle.isNotEmpty(),
        ) { Text("Enregistrer") }
    }
}

@Composable
private fun CarteGoogle(client: MotoBleClient, s: EtatSynchro) {
    var identifiant by rememberSaveable { mutableStateOf("") }
    var secret by rememberSaveable { mutableStateOf("") }
    var jeton by rememberSaveable { mutableStateOf("") }
    Carte("Compte Google (photos et trames CAN)") {
        Text(
            "Client OAuth et jeton de rafraîchissement créés en suivant docs/synchro_maison.md. " +
                "Les photos vont dans l'album « Moto ESP32 » de Google Photos.",
            style = MaterialTheme.typography.bodySmall,
        )
        Ligne("Client OAuth", if (s.clientGoogleRegle) "✅ réglé" else "pas réglé")
        Ligne("Jeton", if (s.jetonGoogleRegle) "✅ réglé" else "pas réglé")
        OutlinedTextField(identifiant, { identifiant = it.trim().take(200) }, label = { Text("ID client (…apps.googleusercontent.com)") },
            singleLine = true, modifier = Modifier.fillMaxWidth())
        ChampSecret(secret, { secret = it.trim().take(100) }, "Code secret du client")
        FilledTonalButton(onClick = { client.envoyerCommande("SYNCHRO_GOOGLE", identifiant, secret); secret = "" },
            enabled = identifiant.isNotEmpty()) { Text("Enregistrer le client") }
        ChampSecret(jeton, { jeton = it.trim().take(400) }, "Jeton de rafraîchissement (1//…)")
        FilledTonalButton(onClick = { client.envoyerCommande("SYNCHRO_GOOGLE_JETON $jeton"); jeton = "" },
            enabled = jeton.isNotEmpty()) { Text("Enregistrer le jeton") }
    }
}

@Composable
private fun CarteTramesCan(client: MotoBleClient, s: EtatSynchro) {
    Carte("Trames CAN brutes") {
        Text(
            "Plus de 100 Mo par heure de route : trop pour une base gratuite. Par défaut, chaque fichier part entier " +
                "dans Google Drive (dossier « Moto ESP32 - trames CAN »), une fois la moto coupée.",
            style = MaterialTheme.typography.bodySmall,
        )
        Column {
            listOf(0 to "Carte SD seule", 1 to "Google Drive", 2 to "Base (serveur maison)").forEach { (mode, titre) ->
                FilterChip(selected = s.modeCan == mode, onClick = { client.envoyerCommande("SYNCHRO_CAN $mode") }, label = { Text(titre) })
            }
        }
    }
}

@Composable
private fun ChampSecret(valeur: String, changer: (String) -> Unit, libelle: String) {
    OutlinedTextField(
        valeur, changer, label = { Text(libelle) }, singleLine = true, modifier = Modifier.fillMaxWidth(),
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
    )
}
