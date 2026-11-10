package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import fr.meudo.motoesp32.ble.EtatCamera
import fr.meudo.motoesp32.ble.MotoBleClient
import kotlin.math.roundToInt

/**
 * Description d'un réglage de la caméra. Les noms, limites et significations sont ceux
 * du tableau REGLAGES de src/module_camera.cpp : les garder identiques.
 */
private data class ReglageCamera(
    val cle: String,
    val libelle: String,
    val minimum: Int = 0,
    val maximum: Int = 1,
    val choix: List<String>? = null,      // liste déroulante (valeur = position dans la liste + minimum)
    val interrupteur: Boolean = false,    // 0 / 1
    val aide: String? = null,
    val actifSi: Pair<String, Int>? = null, // réglage utile seulement si un autre a cette valeur
)

private val TAILLES = listOf(
    "96 × 96", "160 × 120 (QQVGA)", "176 × 144 (QCIF)", "240 × 176 (HQVGA)", "240 × 240",
    "320 × 240 (QVGA)", "400 × 296 (CIF)", "480 × 320 (HVGA)", "640 × 480 (VGA)", "800 × 600 (SVGA)",
    "1024 × 768 (XGA)", "1280 × 720 (HD)", "1280 × 1024 (SXGA)", "1600 × 1200 (UXGA)",
)

private val GROUPES: List<Pair<String, List<ReglageCamera>>> = listOf(
    "Image" to listOf(
        ReglageCamera("taille", "Résolution", 0, 13, choix = TAILLES),
        ReglageCamera("qualite", "Compression JPEG", 4, 63,
            aide = "4 = meilleure qualité (fichier plus gros), 63 = qualité la plus faible"),
        ReglageCamera("lum", "Luminosité", -2, 2),
        ReglageCamera("contraste", "Contraste", -2, 2),
        ReglageCamera("satur", "Saturation", -2, 2),
        ReglageCamera("nettete", "Netteté", -2, 2),
        ReglageCamera("debruit", "Réduction du bruit", 0, 8),
        ReglageCamera("effet", "Effet", 0, 6,
            choix = listOf("Aucun", "Négatif", "Noir et blanc", "Teinte rouge", "Teinte verte", "Teinte bleue", "Sépia")),
    ),
    "Balance des blancs" to listOf(
        ReglageCamera("awb", "Balance des blancs automatique", interrupteur = true),
        ReglageCamera("awb_gain", "Gain de la balance des blancs", interrupteur = true),
        ReglageCamera("wb_mode", "Éclairage", 0, 4,
            choix = listOf("Automatique", "Soleil", "Nuageux", "Bureau (néon)", "Maison (ampoule)"),
            actifSi = "awb" to 1),
    ),
    "Exposition" to listOf(
        ReglageCamera("aec", "Exposition automatique", interrupteur = true),
        ReglageCamera("aec2", "Exposition automatique avancée (DSP)", interrupteur = true, actifSi = "aec" to 1),
        ReglageCamera("ae_niv", "Correction d'exposition", -2, 2, actifSi = "aec" to 1),
        ReglageCamera("aec_val", "Exposition manuelle", 0, 1200, actifSi = "aec" to 0,
            aide = "Utilisée quand l'exposition automatique est coupée"),
    ),
    "Gain (sensibilité)" to listOf(
        ReglageCamera("agc", "Gain automatique", interrupteur = true),
        ReglageCamera("gain_max", "Gain maximum", 0, 6,
            choix = listOf("2x", "4x", "8x", "16x", "32x", "64x", "128x"), actifSi = "agc" to 1),
        ReglageCamera("agc_gain", "Gain manuel", 0, 30, actifSi = "agc" to 0,
            aide = "0 = 1x … 30 = 31x, utilisé quand le gain automatique est coupé"),
    ),
    "Corrections du capteur" to listOf(
        ReglageCamera("bpc", "Correction des pixels noirs (BPC)", interrupteur = true),
        ReglageCamera("wpc", "Correction des pixels blancs (WPC)", interrupteur = true),
        ReglageCamera("gamma", "Correction gamma", interrupteur = true),
        ReglageCamera("lenc", "Correction de l'objectif (vignettage)", interrupteur = true),
        ReglageCamera("dcw", "Réduction de taille (DCW)", interrupteur = true),
    ),
    "Orientation et test" to listOf(
        ReglageCamera("miroir", "Miroir (gauche ↔ droite)", interrupteur = true),
        ReglageCamera("retourne", "Retourner (haut ↔ bas)", interrupteur = true),
        ReglageCamera("mire", "Mire de test (barres de couleur)", interrupteur = true),
    ),
    "Prise de vue" to listOf(
        ReglageCamera("jeter", "Images jetées avant la photo", 0, 10,
            aide = "Laisse l'exposition s'ajuster : augmenter si les photos sont trop claires ou trop sombres"),
    ),
)

/** Cartes de réglage de la caméra, affichées sous la prise de photo. */
@Composable
fun ReglagesDeLaCamera(camera: EtatCamera?, client: MotoBleClient) {
    var confirmerDefaut by remember { mutableStateOf(false) }

    LaunchedEffect(camera == null) { if (camera == null) client.envoyerCommande("CAMERA_REGLAGES") }

    if (camera == null) {
        Carte("Réglages de l'image") { Text("En attente des réglages du module…") }
        return
    }

    Carte("Réglages de l'image") {
        Ligne("Capteur", camera.capteur.ifEmpty { "non démarré" })
        Text(
            "Chaque réglage est appliqué tout de suite et gardé par le module, même après un redémarrage. " +
                "Prenez une photo pour voir le résultat." +
                if (!camera.prete) "\nCaméra non démarrée : les réglages seront appliqués à son prochain démarrage." else "",
            style = MaterialTheme.typography.bodySmall,
        )
        OutlinedButton(onClick = { confirmerDefaut = true }, modifier = Modifier.fillMaxWidth()) {
            Text("Remettre les réglages par défaut")
        }
    }

    GROUPES.forEach { (titre, reglages) ->
        Carte(titre) {
            reglages.forEach { reglage -> UnReglage(reglage, camera, client) }
        }
    }

    if (confirmerDefaut) {
        AlertDialog(
            onDismissRequest = { confirmerDefaut = false },
            title = { Text("Réglages par défaut") },
            text = { Text("Remettre tous les réglages de la caméra à leur valeur d'origine ?") },
            confirmButton = {
                TextButton(onClick = { confirmerDefaut = false; client.envoyerCommande("CAMERA_DEFAUT") }) { Text("Remettre") }
            },
            dismissButton = { TextButton(onClick = { confirmerDefaut = false }) { Text("Annuler") } },
        )
    }
}

@Composable
private fun UnReglage(reglage: ReglageCamera, camera: EtatCamera, client: MotoBleClient) {
    val valeurDuModule = camera.valeurs[reglage.cle] ?: return   // réglage inconnu de ce firmware
    val nonPrisEnCharge = reglage.cle in camera.nonPrisEnCharge
    val actif = !nonPrisEnCharge && (reglage.actifSi?.let { (cle, valeur) -> camera.valeurs[cle] == valeur } ?: true)
    val envoyer = { valeur: Int -> client.envoyerCommande("CAMERA_REGLER", reglage.cle, valeur.toString()) }

    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(2.dp)) {
        when {
            reglage.interrupteur -> Row(verticalAlignment = Alignment.CenterVertically) {
                Text(reglage.libelle, Modifier.weight(1f))
                Switch(checked = valeurDuModule == 1, enabled = actif, onCheckedChange = { envoyer(if (it) 1 else 0) })
            }

            reglage.choix != null -> {
                val maximum = if (reglage.cle == "taille") camera.tailleMaximum else reglage.maximum
                var ouvert by remember { mutableStateOf(false) }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(reglage.libelle, Modifier.weight(1f))
                    Box {
                        OutlinedButton(onClick = { ouvert = true }, enabled = actif) {
                            Text(reglage.choix.getOrElse(valeurDuModule - reglage.minimum) { valeurDuModule.toString() })
                        }
                        DropdownMenu(expanded = ouvert, onDismissRequest = { ouvert = false }) {
                            for (valeur in reglage.minimum..maximum) {
                                DropdownMenuItem(
                                    text = { Text(reglage.choix.getOrElse(valeur - reglage.minimum) { valeur.toString() }) },
                                    onClick = { ouvert = false; envoyer(valeur) },
                                )
                            }
                        }
                    }
                }
            }

            else -> {
                var position by remember(valeurDuModule) { mutableFloatStateOf(valeurDuModule.toFloat()) }
                val ecart = reglage.maximum - reglage.minimum
                Text("${reglage.libelle} : ${afficher(position.roundToInt(), reglage)}")
                Slider(
                    value = position,
                    onValueChange = { position = it },
                    onValueChangeFinished = { envoyer(position.roundToInt()) },
                    valueRange = reglage.minimum.toFloat()..reglage.maximum.toFloat(),
                    steps = if (ecart in 2..40) ecart - 1 else 0,
                    enabled = actif,
                )
            }
        }
        when {
            nonPrisEnCharge -> Text("Non pris en charge par le capteur ${camera.capteur}",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.outline)
            reglage.aide != null -> Text(reglage.aide, style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.outline)
        }
    }
}

private fun afficher(valeur: Int, reglage: ReglageCamera): String =
    if (reglage.minimum < 0 && valeur > 0) "+$valeur" else valeur.toString()
