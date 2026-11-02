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
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material.icons.filled.NetworkCheck
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import fr.meudo.motoesp32.ble.MotoBleClient

@Composable
fun EcranCommandes(modifier: Modifier, client: MotoBleClient) {
    var confirmerRedemarrage by remember { mutableStateOf(false) }
    var commandeLibre by remember { mutableStateOf("") }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Carte("Actions") {
            BoutonCommande("Prendre une photo (carte SD)", Icons.Filled.CameraAlt) { client.envoyerCommande("PHOTO") }
            BoutonCommande("Actualiser la position GPS", Icons.Filled.MyLocation) { client.envoyerCommande("GPS") }
            BoutonCommande("Actualiser toutes les données", Icons.Filled.Refresh) { client.envoyerCommande("STATUS") }
            BoutonCommande("Tester la liaison", Icons.Filled.NetworkCheck) { client.envoyerCommande("PING") }
        }

        Carte("Module") {
            Button(
                onClick = { confirmerRedemarrage = true },
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.RestartAlt, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Redémarrer le module")
            }
        }

        Carte("Commande manuelle") {
            Text("Pour tester une nouvelle commande du firmware.", style = MaterialTheme.typography.bodySmall)
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = commandeLibre,
                    onValueChange = { commandeLibre = it.take(60) },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("ex. LOGS OFF") },
                )
                TextButton(
                    onClick = { client.envoyerCommande(commandeLibre.trim()); commandeLibre = "" },
                    enabled = commandeLibre.isNotBlank(),
                ) { Text("Envoyer") }
            }
        }
    }

    if (confirmerRedemarrage) {
        AlertDialog(
            onDismissRequest = { confirmerRedemarrage = false },
            title = { Text("Redémarrer le module ?") },
            text = { Text("La connexion Bluetooth sera coupée pendant quelques secondes.") },
            confirmButton = {
                TextButton(onClick = {
                    confirmerRedemarrage = false
                    client.envoyerCommande("REBOOT")
                }) { Text("Redémarrer") }
            },
            dismissButton = { TextButton(onClick = { confirmerRedemarrage = false }) { Text("Annuler") } },
        )
    }
}

@Composable
private fun BoutonCommande(texte: String, icone: ImageVector, action: () -> Unit) {
    FilledTonalButton(onClick = action, modifier = Modifier.fillMaxWidth()) {
        Icon(icone, contentDescription = null)
        Spacer(Modifier.size(8.dp))
        Text(texte, modifier = Modifier.weight(1f))
    }
}
