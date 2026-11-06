package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.MotoBleClient

@Composable
fun EcranLogs(modifier: Modifier, client: MotoBleClient) {
    val logs by client.logs.collectAsStateWithLifecycle()
    var suivre by remember { mutableStateOf(true) }
    var logsModule by remember { mutableStateOf(true) }
    val liste = rememberLazyListState()

    LaunchedEffect(logs.size, suivre) {
        if (suivre && logs.isNotEmpty()) liste.scrollToItem(logs.lastIndex)
    }

    Column(modifier.padding(horizontal = 12.dp)) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            FilterChip(selected = suivre, onClick = { suivre = !suivre }, label = { Text("Défilement auto") })
            FilterChip(
                selected = logsModule,
                onClick = {
                    logsModule = !logsModule
                    client.envoyerCommande(if (logsModule) "LOGS ON" else "LOGS OFF")
                },
                label = { Text("Recevoir") },
            )
            TextButton(onClick = { client.effacerLogs() }) { Text("Effacer") }
        }
        if (logs.isEmpty()) {
            Text("Aucun log reçu pour l'instant.", modifier = Modifier.padding(8.dp))
        }
        LazyColumn(state = liste, modifier = Modifier.fillMaxWidth()) {
            itemsIndexed(logs) { _, ligne ->
                Text(
                    ligne,
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(vertical = 2.dp),
                )
            }
        }
    }
}
