package fr.meudo.motoesp32.ui

import android.graphics.BitmapFactory
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.CreateNewFolder
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.UploadFile
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.ElementFichier
import fr.meudo.motoesp32.ble.EtatEnvoi
import fr.meudo.motoesp32.ble.EtatTelechargement
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Ce que l'utilisateur est en train de faire sur un fichier (fenêtre affichée). */
private sealed interface Dialogue {
    data class Renommer(val element: ElementFichier) : Dialogue
    data class Deplacer(val element: ElementFichier) : Dialogue
    data class Supprimer(val element: ElementFichier) : Dialogue
    data object NouveauDossier : Dialogue
}

@Composable
fun EcranFichiers(modifier: Modifier, client: MotoBleClient) {
    val dossier by client.dossier.collectAsStateWithLifecycle()
    val telechargement by client.telechargement.collectAsStateWithLifecycle()
    val envoi by client.envoi.collectAsStateWithLifecycle()
    var dialogue by remember { mutableStateOf<Dialogue?>(null) }
    var fichierAEnregistrer by remember { mutableStateOf<Pair<String, ByteArray>?>(null) }
    var apercu by remember { mutableStateOf<Pair<String, ByteArray>?>(null) }
    val portee = rememberCoroutineScope()
    val context = LocalContext.current

    // Fenêtre Android « Enregistrer sous » pour copier le fichier téléchargé sur le téléphone.
    val enregistrerSous = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("*/*")) { uri: Uri? ->
        val contenu = fichierAEnregistrer?.second
        if (uri != null && contenu != null) {
            portee.launch(Dispatchers.IO) {
                context.contentResolver.openOutputStream(uri)?.use { it.write(contenu) }
            }
        }
        fichierAEnregistrer = null
    }

    // Fenêtre Android « choisir un fichier » pour l'envoyer dans le dossier affiché de la carte SD.
    val choisirUnFichierAEnvoyer = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        val dossierDeDestination = dossier.chemin
        portee.launch {
            val lu = kotlinx.coroutines.withContext(Dispatchers.IO) { lireUnFichierDuTelephone(context, uri) }
            when {
                lu == null -> client.signalerUneErreurDEnvoi("Lecture du fichier impossible")
                lu.second.size > TAILLE_MAXIMUM_D_UN_ENVOI -> client.signalerUneErreurDEnvoi(
                    "Fichier trop gros (${Protocole.taille(lu.second.size.toLong())}, maximum ${Protocole.taille(TAILLE_MAXIMUM_D_UN_ENVOI.toLong())})")
                else -> client.envoyerUnFichier(Protocole.chemin(dossierDeDestination, lu.first), lu.second)
            }
        }
    }

    LaunchedEffect(Unit) { if (dossier.elements.isEmpty()) client.ouvrirUnDossier(dossier.chemin) }

    fun telecharger(element: ElementFichier, pourApercu: Boolean) {
        val chemin = Protocole.chemin(dossier.chemin, element.nom)
        portee.launch {
            val contenu = client.telecharger(chemin, element.taille) ?: return@launch
            if (pourApercu) apercu = element.nom to contenu
            else {
                fichierAEnregistrer = element.nom to contenu
                enregistrerSous.launch(element.nom)
            }
        }
    }

    Column(modifier.padding(horizontal = 12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = { client.ouvrirUnDossier(Protocole.parent(dossier.chemin)) }, enabled = dossier.chemin != "/") {
                Icon(Icons.Filled.ArrowUpward, contentDescription = "Dossier parent")
            }
            Text(dossier.chemin, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f),
                maxLines = 1, overflow = TextOverflow.Ellipsis)
            IconButton(onClick = { choisirUnFichierAEnvoyer.launch("*/*") }, enabled = envoi !is EtatEnvoi.EnCours) {
                Icon(Icons.Filled.UploadFile, contentDescription = "Envoyer un fichier du téléphone ici")
            }
            IconButton(onClick = { dialogue = Dialogue.NouveauDossier }) { Icon(Icons.Filled.CreateNewFolder, contentDescription = "Nouveau dossier") }
            IconButton(onClick = { client.actualiserLeDossier() }) { Icon(Icons.Filled.Refresh, contentDescription = "Actualiser") }
        }
        if (dossier.chargement) LinearProgressIndicator(Modifier.fillMaxWidth())
        BandeauTelechargement(telechargement, client)
        BandeauEnvoi(envoi, client)

        LazyColumn(Modifier.fillMaxWidth()) {
            if (!dossier.chargement && dossier.elements.isEmpty()) item { Text("Dossier vide (ou carte SD absente).", Modifier.padding(8.dp)) }
            items(dossier.elements, key = { it.nom }) { element ->
                LigneDeFichier(
                    element = element,
                    ouvrir = {
                        when {
                            element.estUnDossier -> client.ouvrirUnDossier(Protocole.chemin(dossier.chemin, element.nom))
                            estUneImage(element.nom) -> telecharger(element, pourApercu = true)
                            else -> telecharger(element, pourApercu = false)
                        }
                    },
                    telecharger = { telecharger(element, pourApercu = false) },
                    renommer = { dialogue = Dialogue.Renommer(element) },
                    deplacer = { dialogue = Dialogue.Deplacer(element) },
                    supprimer = { dialogue = Dialogue.Supprimer(element) },
                    jouer = { client.envoyerCommande("SON_JOUER ${Protocole.chemin(dossier.chemin, element.nom)}") },
                )
                HorizontalDivider()
            }
        }
    }

    dialogue?.let { d -> FenetreDeDialogue(d, dossier.chemin, client) { dialogue = null } }

    apercu?.let { (nom, contenu) ->
        val image = remember(contenu) { BitmapFactory.decodeByteArray(contenu, 0, contenu.size)?.asImageBitmap() }
        AlertDialog(
            onDismissRequest = { apercu = null },
            title = { Text(nom, maxLines = 1, overflow = TextOverflow.Ellipsis) },
            text = {
                if (image != null) Image(image, contentDescription = nom, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxWidth().heightIn(max = 400.dp))
                else Text("Image illisible.")
            },
            confirmButton = {
                TextButton(onClick = {
                    fichierAEnregistrer = nom to contenu
                    apercu = null
                    enregistrerSous.launch(nom)
                }) { Text("Enregistrer sur le téléphone") }
            },
            dismissButton = { TextButton(onClick = { apercu = null }) { Text("Fermer") } },
        )
    }
}

/** Au-delà, l'envoi par Bluetooth serait trop long (environ 20 Ko/s) : mieux vaut passer par le PC. */
private const val TAILLE_MAXIMUM_D_UN_ENVOI = 20 * 1024 * 1024

/** Lit un fichier choisi sur le téléphone : son nom et son contenu. */
private fun lireUnFichierDuTelephone(context: android.content.Context, uri: Uri): Pair<String, ByteArray>? = try {
    val nom = context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
        ?.use { curseur -> if (curseur.moveToFirst()) curseur.getString(0) else null }
        ?: uri.lastPathSegment?.substringAfterLast('/')
        ?: "fichier"
    val contenu = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
    // Les caractères « / » et « | » ont un sens dans les commandes du module : on les remplace.
    if (contenu == null) null else nom.replace('/', '_').replace('|', '_') to contenu
} catch (e: Exception) {
    null
}

@Composable
private fun BandeauEnvoi(etat: EtatEnvoi, client: MotoBleClient) {
    when (etat) {
        is EtatEnvoi.EnCours -> Column(Modifier.padding(vertical = 6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Envoi de ${etat.chemin.substringAfterLast('/')} : ${Protocole.taille(etat.envoyes)} / ${Protocole.taille(etat.total)}",
                    Modifier.weight(1f), style = MaterialTheme.typography.bodySmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                TextButton(onClick = { client.annulerLEnvoi() }) { Text("Annuler") }
            }
            LinearProgressIndicator(
                progress = { if (etat.total > 0) etat.envoyes.toFloat() / etat.total else 0f },
                modifier = Modifier.fillMaxWidth(),
            )
        }
        is EtatEnvoi.Termine -> Row(verticalAlignment = Alignment.CenterVertically) {
            Text("✅ ${etat.chemin.substringAfterLast('/')} envoyé sur la carte SD", Modifier.weight(1f))
            TextButton(onClick = { client.oublierLEnvoi() }) { Text("OK") }
        }
        is EtatEnvoi.Erreur -> Row(verticalAlignment = Alignment.CenterVertically) {
            Text("⚠️ Envoi : ${etat.message}", Modifier.weight(1f), color = MaterialTheme.colorScheme.error)
            TextButton(onClick = { client.oublierLEnvoi() }) { Text("OK") }
        }
        EtatEnvoi.Inactif -> {}
    }
}

fun estUneImage(nom: String): Boolean = nom.lowercase().let { it.endsWith(".jpg") || it.endsWith(".jpeg") || it.endsWith(".png") }

@Composable
private fun BandeauTelechargement(etat: EtatTelechargement, client: MotoBleClient) {
    when (etat) {
        is EtatTelechargement.EnCours -> Column(Modifier.padding(vertical = 6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Téléchargement : ${Protocole.taille(etat.recus)} / ${Protocole.taille(etat.total)}", Modifier.weight(1f),
                    style = MaterialTheme.typography.bodySmall)
                TextButton(onClick = { client.annulerLeTelechargement() }) { Text("Annuler") }
            }
            LinearProgressIndicator(
                progress = { if (etat.total > 0) etat.recus.toFloat() / etat.total else 0f },
                modifier = Modifier.fillMaxWidth(),
            )
        }
        is EtatTelechargement.Erreur -> Row(verticalAlignment = Alignment.CenterVertically) {
            Text("⚠️ ${etat.message}", Modifier.weight(1f), color = MaterialTheme.colorScheme.error)
            TextButton(onClick = { client.oublierLeTelechargement() }) { Text("OK") }
        }
        else -> {}
    }
}

@Composable
private fun LigneDeFichier(
    element: ElementFichier,
    ouvrir: () -> Unit,
    telecharger: () -> Unit,
    renommer: () -> Unit,
    deplacer: () -> Unit,
    supprimer: () -> Unit,
    jouer: () -> Unit,
) {
    var menuOuvert by remember { mutableStateOf(false) }
    Row(Modifier.fillMaxWidth().clickable(onClick = ouvrir).padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Icon(
            when {
                element.estUnDossier -> Icons.Filled.Folder
                estUneImage(element.nom) -> Icons.Filled.Image
                else -> Icons.Filled.Description
            },
            contentDescription = null,
            tint = if (element.estUnDossier) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
            Text(element.nom, maxLines = 1, overflow = TextOverflow.Ellipsis)
            val details = buildList {
                if (!element.estUnDossier) add(Protocole.taille(element.taille))
                if (element.dateModification > 0) add(SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE).format(Date(element.dateModification * 1000)))
            }.joinToString(" · ")
            if (details.isNotEmpty()) Text(details, style = MaterialTheme.typography.bodySmall)
        }
        IconButton(onClick = { menuOuvert = true }) { Icon(Icons.Filled.MoreVert, contentDescription = "Actions") }
        DropdownMenu(expanded = menuOuvert, onDismissRequest = { menuOuvert = false }) {
            if (estUnSon(element.nom)) DropdownMenuItem(text = { Text("Jouer sur le haut-parleur") }, onClick = { menuOuvert = false; jouer() })
            if (!element.estUnDossier) DropdownMenuItem(text = { Text("Télécharger") }, onClick = { menuOuvert = false; telecharger() })
            DropdownMenuItem(text = { Text("Renommer") }, onClick = { menuOuvert = false; renommer() })
            DropdownMenuItem(text = { Text("Déplacer") }, onClick = { menuOuvert = false; deplacer() })
            DropdownMenuItem(text = { Text("Supprimer") }, onClick = { menuOuvert = false; supprimer() })
        }
    }
}

@Composable
private fun FenetreDeDialogue(dialogue: Dialogue, dossierCourant: String, client: MotoBleClient, fermer: () -> Unit) {
    val valeurDeDepart = when (dialogue) {
        is Dialogue.Renommer -> dialogue.element.nom
        is Dialogue.Deplacer -> Protocole.chemin(dossierCourant, dialogue.element.nom)
        else -> ""
    }
    var saisie by remember { mutableStateOf(valeurDeDepart) }

    val (titre, explication, action) = when (dialogue) {
        is Dialogue.Renommer -> Triple("Renommer", "Nouveau nom :", "Renommer")
        is Dialogue.Deplacer -> Triple("Déplacer", "Nouveau chemin complet (les dossiers manquants sont créés) :", "Déplacer")
        is Dialogue.Supprimer -> Triple(
            "Supprimer ?",
            if (dialogue.element.estUnDossier) "Le dossier « ${dialogue.element.nom} » et TOUT son contenu seront effacés." else "« ${dialogue.element.nom} » sera effacé.",
            "Supprimer",
        )
        Dialogue.NouveauDossier -> Triple("Nouveau dossier", "Nom du dossier :", "Créer")
    }

    AlertDialog(
        onDismissRequest = fermer,
        title = { Text(titre) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(explication)
                if (dialogue !is Dialogue.Supprimer) {
                    OutlinedTextField(value = saisie, onValueChange = { saisie = it.replace("|", "") }, singleLine = true)
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = dialogue is Dialogue.Supprimer || saisie.isNotBlank(),
                onClick = {
                    when (dialogue) {
                        is Dialogue.Renommer -> client.envoyerCommande("FICHIER_RENOMMER",
                            Protocole.chemin(dossierCourant, dialogue.element.nom), Protocole.chemin(dossierCourant, saisie.trim()))
                        is Dialogue.Deplacer -> client.envoyerCommande("FICHIER_DEPLACER",
                            Protocole.chemin(dossierCourant, dialogue.element.nom),
                            saisie.trim().let { if (it.startsWith("/")) it else "/$it" })
                        is Dialogue.Supprimer -> client.envoyerCommande("FICHIER_SUPPRIMER ${Protocole.chemin(dossierCourant, dialogue.element.nom)}")
                        Dialogue.NouveauDossier -> client.envoyerCommande("DOSSIER_CREER ${Protocole.chemin(dossierCourant, saisie.trim())}")
                    }
                    client.actualiserLeDossier()
                    fermer()
                },
            ) { Text(action) }
        },
        dismissButton = { TextButton(onClick = fermer) { Text("Annuler") } },
    )
}

/** Formats que le modem sait jouer (AT+CCMXPLAY). */
private fun estUnSon(nom: String): Boolean =
    nom.substringAfterLast('.', "").lowercase() in setOf("wav", "amr", "mp3")
