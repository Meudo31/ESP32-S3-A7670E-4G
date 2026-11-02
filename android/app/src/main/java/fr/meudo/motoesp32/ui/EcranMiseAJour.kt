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
    val telemetrie by client.telemetrie.collectAsStateWithLifecycle()
    val choixFichier = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.mettreAJour(uri)
    }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Carte("Firmware actuel") {
            val s = telemetrie.systeme
            Text(if (s == null) "Version inconnue" else "${s.versionFirmware} — compilé le ${s.compilation}")
        }

        Carte("Mettre à jour par Bluetooth") {
            Text(
                "1. Compiler le firmware avec PlatformIO.\n" +
                    "2. Copier .pio/build/esp32-s3-devkitc-1/firmware.bin sur le téléphone.\n" +
                    "3. Choisir ce fichier ci-dessous. Garder le téléphone près de la moto (1 à 3 minutes).",
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
