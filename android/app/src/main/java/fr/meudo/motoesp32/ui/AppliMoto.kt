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
import androidx.compose.material.icons.filled.BatteryChargingFull
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Lightbulb
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.Phone
import androidx.compose.material.icons.filled.LinkOff
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.rememberDrawerState
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.launch
import fr.meudo.motoesp32.MotoViewModel
import fr.meudo.motoesp32.ble.EtatAppel
import fr.meudo.motoesp32.ble.EtatConnexion
import fr.meudo.motoesp32.ble.Evenement
import fr.meudo.motoesp32.ble.EtatOta

/** Les écrans de l'application, dans l'ordre du menu. */
enum class Ecran(val titre: String, val icone: ImageVector) {
    TABLEAU("Tableau de bord", Icons.Filled.Dashboard),
    GPS("GPS et carte", Icons.Filled.Map),
    TELEPHONE("SMS, appels, 4G", Icons.Filled.Phone),
    PHOTO("Photo", Icons.Filled.CameraAlt),
    FICHIERS("Fichiers de la carte SD", Icons.Filled.Folder),
    LUMIERES("Lumières et son", Icons.Filled.Lightbulb),
    BATTERIE("Batterie et alimentation", Icons.Filled.BatteryChargingFull),
    SYSTEME("Module (mémoire, processeur)", Icons.Filled.Memory),
    JOURNAL("Journal", Icons.Filled.Terminal),
    MISE_A_JOUR("Mise à jour du firmware", Icons.Filled.SystemUpdate),
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
    val etatModule by client.etatModule.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var ecran by rememberSaveable { mutableStateOf(Ecran.TABLEAU) }
    val tiroir = rememberDrawerState(DrawerValue.Closed)
    val portee = rememberCoroutineScope()

    LaunchedEffect(Unit) {
        client.reponses.collect { r ->
            if (!r.ok || r.commande in COMMANDES_A_CONFIRMER) {
                snackbar.showSnackbar((if (r.ok) "✅ " else "⚠️ ") + "${r.commande} : ${r.message}")
            }
        }
    }
    LaunchedEffect(Unit) {
        client.evenements.collect { e ->
            when (e) {
                is Evenement.SmsRecu -> snackbar.showSnackbar("✉️ SMS de ${e.sms.expediteur} : ${e.sms.texte}")
                is Evenement.ResultatEnvoiSms -> snackbar.showSnackbar(if (e.ok) "✅ SMS envoyé à ${e.numero}" else "⚠️ Échec de l'envoi à ${e.numero}")
                else -> {}
            }
        }
    }

    // Pendant une reconnexion automatique, on garde les écrans et les dernières données affichées.
    val enReconnexion = etat is EtatConnexion.Reconnexion
    val connecte = etat == EtatConnexion.Connecte || enReconnexion

    // Si le compteur de démarrages du module augmente pendant qu'on l'utilise, c'est qu'il a redémarré tout seul
    // (coupure d'alimentation, plantage) : on le dit, avec la raison, pour comprendre les déconnexions.
    var demarragesConnus by remember { mutableStateOf<Long?>(null) }
    val systeme = etatModule.systeme
    LaunchedEffect(systeme?.nombreDeDemarrages) {
        val demarrages = systeme?.nombreDeDemarrages ?: return@LaunchedEffect
        val avant = demarragesConnus
        demarragesConnus = demarrages
        if (avant != null && demarrages > avant) {
            snackbar.showSnackbar("⚠️ Le module a redémarré tout seul (${systeme.raisonRedemarrage})")
        }
    }
    ModalNavigationDrawer(
        drawerState = tiroir,
        gesturesEnabled = connecte,
        drawerContent = {
            ModalDrawerSheet {
                Text("Moto ESP32", Modifier.padding(16.dp), style = MaterialTheme.typography.titleLarge)
                Ecran.entries.forEach { e ->
                    NavigationDrawerItem(
                        label = { Text(e.titre) },
                        icon = { Icon(e.icone, contentDescription = null) },
                        selected = e == ecran,
                        onClick = { ecran = e; portee.launch { tiroir.close() } },
                        modifier = Modifier.padding(horizontal = 12.dp),
                    )
                }
            }
        },
    ) {
        Scaffold(
            topBar = {
                TopAppBar(
                    navigationIcon = {
                        if (connecte) IconButton(onClick = { portee.launch { tiroir.open() } }) { Icon(Icons.Filled.Menu, contentDescription = "Menu") }
                    },
                    title = {
                        Column {
                            Text(if (connecte) ecran.titre else "Moto ESP32")
                            Text(texteEtat(etat), style = MaterialTheme.typography.bodySmall)
                        }
                    },
                    actions = {
                        if (connecte) {
                            IconButton(onClick = { client.deconnecter() }) {
                                Icon(Icons.Filled.LinkOff, contentDescription = "Déconnecter")
                            }
                        }
                    },
                )
            },
            snackbarHost = { SnackbarHost(snackbar) },
        ) { marges ->
            val modifier = Modifier.padding(marges).fillMaxSize()
            when {
                !permissionsAccordees -> EcranPermissions(modifier, demanderPermissions)
                !connecte -> EcranConnexion(modifier, viewModel, activerBluetooth)
                else -> when (ecran) {
                    Ecran.TABLEAU -> EcranTableauDeBord(modifier, client) { ecran = it }
                    Ecran.GPS -> EcranGps(modifier, client)
                    Ecran.TELEPHONE -> EcranTelephone(modifier, client)
                    Ecran.PHOTO -> EcranCamera(modifier, client)
                    Ecran.FICHIERS -> EcranFichiers(modifier, client)
                    Ecran.LUMIERES -> EcranNeopixels(modifier, client)
                    Ecran.BATTERIE -> EcranBatterie(modifier, client)
                    Ecran.SYSTEME -> EcranSysteme(modifier, client)
                    Ecran.JOURNAL -> EcranLogs(modifier, client)
                    Ecran.MISE_A_JOUR -> EcranMiseAJour(modifier, viewModel)
                }
            }
        }
    }

    // Appel entrant : fenêtre par-dessus n'importe quel écran.
    val appel = etatModule.appel
    if (etat == EtatConnexion.Connecte && appel != null && appel.etat == EtatAppel.Etat.SONNERIE) {
        AlertDialog(
            onDismissRequest = {},
            title = { Text("📞 Appel entrant") },
            text = { Text(appel.numero.ifEmpty { "Numéro masqué" }, style = MaterialTheme.typography.headlineSmall) },
            confirmButton = { BoutonsDAppel(client, appel.etat) },
        )
    }
}

/** Commandes dont on affiche aussi la réussite (les autres ne s'affichent qu'en cas d'erreur). */
private val COMMANDES_A_CONFIRMER = setOf(
    "FICHIER_RENOMMER", "FICHIER_DEPLACER", "FICHIER_SUPPRIMER", "DOSSIER_CREER", "SMS_SUPPRIMER",
    "SONS_INSTALLER", "REBOOT", "PING", "APPELER",
)

private fun texteEtat(etat: EtatConnexion): String = when (etat) {
    EtatConnexion.Deconnecte -> "Déconnecté"
    EtatConnexion.Recherche -> "Recherche du module…"
    is EtatConnexion.Connexion -> etat.etape
    is EtatConnexion.Reconnexion -> "⚠️ Liaison perdue, reconnexion… (essai ${etat.tentative})"
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

