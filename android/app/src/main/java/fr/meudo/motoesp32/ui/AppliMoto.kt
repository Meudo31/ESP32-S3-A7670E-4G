package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.LinkOff
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.MotoViewModel
import fr.meudo.motoesp32.ble.EtatConnexion
import fr.meudo.motoesp32.ble.EtatOta

private enum class Onglet(val titre: String, val icone: ImageVector) {
    TABLEAU("Tableau", Icons.Filled.Dashboard),
    COMMANDES("Commandes", Icons.Filled.TouchApp),
    LOGS("Logs", Icons.Filled.Terminal),
    MISE_A_JOUR("Mise à jour", Icons.Filled.SystemUpdate),
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppliMoto(
    viewModel: MotoViewModel,
    permissionsAccordees: Boolean,
    demanderPermissions: () -> Unit,
    activerBluetooth: () -> Unit,
) {
    val client = viewModel.client
    val etat by client.etat.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var onglet by rememberSaveable { mutableIntStateOf(0) }

    LaunchedEffect(Unit) {
        client.reponses.collect { r ->
            snackbar.showSnackbar((if (r.ok) "✅ " else "⚠️ ") + "${r.commande} : ${r.message}")
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("Moto ESP32")
                        Text(texteEtat(etat), style = MaterialTheme.typography.bodySmall)
                    }
                },
                actions = {
                    if (etat == EtatConnexion.Connecte) {
                        IconButton(onClick = { client.deconnecter() }) {
                            Icon(Icons.Filled.LinkOff, contentDescription = "Déconnecter")
                        }
                    }
                },
            )
        },
        bottomBar = {
            if (etat == EtatConnexion.Connecte) {
                NavigationBar {
                    Onglet.entries.forEachIndexed { index, o ->
                        NavigationBarItem(
                            selected = onglet == index,
                            onClick = { onglet = index },
                            icon = { Icon(o.icone, contentDescription = null) },
                            label = { Text(o.titre) },
                        )
                    }
                }
            }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { marges ->
        val modifier = Modifier.padding(marges).fillMaxSize()
        when {
            !permissionsAccordees -> EcranPermissions(modifier, demanderPermissions)
            etat != EtatConnexion.Connecte -> EcranConnexion(modifier, viewModel, activerBluetooth)
            else -> when (Onglet.entries[onglet]) {
                Onglet.TABLEAU -> EcranTableauDeBord(modifier, client)
                Onglet.COMMANDES -> EcranCommandes(modifier, client)
                Onglet.LOGS -> EcranLogs(modifier, client)
                Onglet.MISE_A_JOUR -> EcranMiseAJour(modifier, viewModel)
            }
        }
    }
}

private fun texteEtat(etat: EtatConnexion): String = when (etat) {
    EtatConnexion.Deconnecte -> "Déconnecté"
    EtatConnexion.Recherche -> "Recherche du module…"
    is EtatConnexion.Connexion -> etat.etape
    EtatConnexion.Connecte -> "Connecté"
    is EtatConnexion.Erreur -> etat.message
}

@Composable
private fun EcranPermissions(modifier: Modifier, demander: () -> Unit) {
    Column(
        modifier.padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(Icons.Filled.Bluetooth, contentDescription = null, modifier = Modifier.size(64.dp))
        Spacer(Modifier.height(16.dp))
        Text(
            "L'application a besoin du Bluetooth pour trouver le module de la moto et recevoir ses données.",
            style = MaterialTheme.typography.bodyLarge,
        )
        Spacer(Modifier.height(16.dp))
        Button(onClick = demander) { Text("Autoriser le Bluetooth") }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun EcranConnexion(modifier: Modifier, viewModel: MotoViewModel, activerBluetooth: () -> Unit) {
    val client = viewModel.client
    val etat by client.etat.collectAsStateWithLifecycle()
    val modules by client.modules.collectAsStateWithLifecycle()
    val ota by client.ota.collectAsStateWithLifecycle()
    val dernier = viewModel.dernierModule

    LazyColumn(modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (ota == EtatOta.Termine) {
            item {
                Carte("Mise à jour réussie") {
                    Text("Le module redémarre sur le nouveau firmware. Reconnectez-vous dans quelques secondes.")
                }
            }
        }
        if (!client.bluetoothActif) {
            item {
                Carte("Bluetooth désactivé") {
                    Button(onClick = activerBluetooth) { Text("Activer le Bluetooth") }
                }
            }
        }
        if (etat is EtatConnexion.Connexion) {
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(24.dp))
                    Spacer(Modifier.size(12.dp))
                    Text((etat as EtatConnexion.Connexion).etape)
                }
            }
            item {
                Text(
                    "Au premier branchement, Android demande un code PIN : c'est SECRET_BLE_PIN, " +
                        "défini dans include/secrets.h du firmware.",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
        if (etat is EtatConnexion.Erreur) {
            item { Text((etat as EtatConnexion.Erreur).message, color = MaterialTheme.colorScheme.error) }
        }
        if (dernier != null && etat !is EtatConnexion.Connexion) {
            item {
                Button(onClick = { viewModel.connecter(dernier) }, modifier = Modifier.fillMaxWidth()) {
                    Text("Se connecter à ma moto ($dernier)")
                }
            }
        }
        item {
            OutlinedButton(
                onClick = { if (etat == EtatConnexion.Recherche) client.arreterRecherche() else client.rechercher() },
                enabled = etat !is EtatConnexion.Connexion,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.BluetoothSearching, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text(if (etat == EtatConnexion.Recherche) "Arrêter la recherche" else "Rechercher un module")
            }
        }
        items(modules, key = { it.adresse }) { m ->
            Card(onClick = { viewModel.connecter(m.adresse) }, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text(m.nom, style = MaterialTheme.typography.titleMedium)
                    Text("${m.adresse} · signal ${m.rssi} dBm", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
        if (etat == EtatConnexion.Recherche && modules.isEmpty()) {
            item { Text("Aucun module trouvé pour l'instant. Le module est-il allumé ?") }
        }
    }
}

/** Carte avec un titre, utilisée sur tous les écrans. */
@Composable
fun Carte(titre: String, modifier: Modifier = Modifier, contenu: @Composable () -> Unit) {
    Card(modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(titre, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
            contenu()
        }
    }
}
