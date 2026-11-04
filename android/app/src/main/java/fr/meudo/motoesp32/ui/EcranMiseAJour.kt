package fr.meudo.motoesp32.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.MotoViewModel
import fr.meudo.motoesp32.ble.EtatOta

@Composable
fun EcranMiseAJour(modifier: Modifier, viewModel: MotoViewModel) {
    val client = viewModel.client
    val ota by client.ota.collectAsStateWithLifecycle()
    val etatModule by client.etatModule.collectAsStateWithLifecycle()
    val choixFichier = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.mettreAJour(uri)
    }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Carte("Firmware actuel") {
            val s = etatModule.systeme
            Text(if (s == null) "Version inconnue" else "${s.versionFirmware} — compilé le ${s.compilation}")
        }

        CarteDuWifi(client)

        Carte("Mettre à jour le firmware") {
            Text(
                "1. Récupérer firmware.bin : artefact « firmware-esp32 » de GitHub Actions, " +
                    "ou .pio/build/esp32-s3-devkitc-1/firmware.bin après une compilation PlatformIO.\n" +
                    "2. Copier firmware.bin sur le téléphone.\n" +
                    "3. Choisir ce fichier ci-dessous. Garder le téléphone près de la moto " +
                    "(environ 20 s en Wi-Fi, 1 à 3 minutes en Bluetooth).",
                style = MaterialTheme.typography.bodyMedium,
            )
            when (val o = ota) {
                EtatOta.Inactif -> Button(
                    onClick = { choixFichier.launch(arrayOf("application/octet-stream", "*/*")) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Choisir le fichier firmware.bin") }

                is EtatOta.EnCours -> {
                    LinearProgressIndicator(progress = { o.progression }, modifier = Modifier.fillMaxWidth())
                    Text(o.message)
                    Text("Gardez l'application ouverte.", style = MaterialTheme.typography.bodySmall)
                }

                EtatOta.Termine -> {
                    Text("✅ Mise à jour réussie. Le module redémarre.")
                    TextButton(onClick = { client.reinitialiserOta() }) { Text("OK") }
                }

                is EtatOta.Erreur -> {
                    Text("⚠️ ${o.message}", color = MaterialTheme.colorScheme.error)
                    Text(
                        "Le module garde son ancien firmware : il n'est jamais remplacé tant que le nouveau n'est pas complet et vérifié.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                    TextButton(onClick = { client.reinitialiserOta() }) { Text("Réessayer") }
                }
            }
        }
    }
}

/** Réglage commun : utiliser le Wi-Fi du module pour les gros transferts. */
@Composable
fun CarteDuWifi(client: fr.meudo.motoesp32.ble.MotoBleClient) {
    val autorise by client.wifiAutorise.collectAsStateWithLifecycle()
    Carte("Wi-Fi pour les gros transferts") {
        androidx.compose.foundation.layout.Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
            Text("Utiliser le Wi-Fi du module", modifier = Modifier.weight(1f))
            androidx.compose.material3.Switch(
                checked = autorise && client.wifiPossible,
                onCheckedChange = { client.autoriserLeWifi(it) },
                enabled = client.wifiPossible,
            )
        }
        Text(
            if (client.wifiPossible)
                "Pour la mise à jour et les fichiers de plus de 100 Ko, le module ouvre son Wi-Fi « Moto_ESP32 » le temps " +
                    "du transfert (environ 50 fois plus rapide). Android demande d'accepter ce réseau la première fois. " +
                    "Internet reste sur la 4G. Si le Wi-Fi échoue, le transfert se fait en Bluetooth."
            else "Nécessite Android 10 ou plus récent : les transferts se font en Bluetooth.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}
