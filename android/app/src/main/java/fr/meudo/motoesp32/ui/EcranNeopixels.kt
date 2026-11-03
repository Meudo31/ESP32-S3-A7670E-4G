package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.clickable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.MotoBleClient

/** Écran « Lumières et son » : animations de la bande NeoPixel et haut-parleur. */
@Composable
fun EcranNeopixels(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val neo = etat.neopixels
    var luminosite by remember { mutableFloatStateOf(neo?.luminosite?.toFloat() ?: 80f) }
    var volume by remember { mutableFloatStateOf(etat.carte?.volume?.toFloat() ?: 4f) }
    var annonce by remember { mutableStateOf("") }

    LaunchedEffect(neo?.luminosite) { neo?.let { luminosite = it.luminosite.toFloat() } }
    LaunchedEffect(etat.carte?.volume) { etat.carte?.let { volume = it.volume.toFloat() } }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Carte("Animation en cours") {
            if (neo == null) {
                Text("En attente des données…")
            } else {
                Text(
                    if (neo.allumee) "${neo.numero + 1}. ${neo.nom}" else "Bande éteinte",
                    style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Bande allumée", Modifier.weight(1f))
                    Switch(checked = neo.allumee, onCheckedChange = { client.envoyerCommande("NEO_ONOFF") })
                }
                FilledTonalButton(onClick = { client.envoyerCommande("NEO_SUIVANTE") }, modifier = Modifier.fillMaxWidth()) {
                    Text("Animation suivante (comme le bouton)")
                }
                Text("Luminosité : ${(luminosite / 2.55f).toInt()} %")
                Slider(
                    value = luminosite, onValueChange = { luminosite = it }, valueRange = 0f..255f,
                    onValueChangeFinished = { client.envoyerCommande("NEO_LUMINOSITE ${luminosite.toInt()}") },
                )
            }
        }

        if (neo != null) {
            Carte("Les 10 animations") {
                neo.noms.forEachIndexed { numero, nom ->
                    Row(
                        Modifier.fillMaxWidth().clickable { client.envoyerCommande("NEO $numero") },
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = numero == neo.numero, onClick = { client.envoyerCommande("NEO $numero") })
                        Text("${numero + 1}. $nom")
                    }
                }
            }
        }

        Carte("Haut-parleur") {
            Text("Volume : ${volume.toInt()} / 5")
            Slider(
                value = volume, onValueChange = { volume = it }, valueRange = 0f..5f, steps = 4,
                onValueChangeFinished = { client.envoyerCommande("VOLUME ${volume.toInt()}") },
            )
            OutlinedTextField(
                value = annonce, onValueChange = { annonce = it.take(60) },
                label = { Text("Faire dire une phrase au module") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { client.envoyerCommande("ANNONCER $annonce") }, enabled = annonce.isNotBlank()) { Text("Dire") }
                TextButton(onClick = { client.envoyerCommande("SONS_INSTALLER") }) { Text("Installer les sons de la carte SD") }
            }
            Text(
                "Sons personnalisés : déposer /sons/sms.wav et /sons/sonnerie.wav sur la carte SD (WAV mono 8 kHz), " +
                    "puis « Installer les sons ». Sans eux, le module prononce une phrase.",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}
