package fr.meudo.motoesp32.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import fr.meudo.motoesp32.ble.EtatTelechargement
import fr.meudo.motoesp32.ble.Evenement
import fr.meudo.motoesp32.ble.MotoBleClient
import fr.meudo.motoesp32.ble.Protocole

/** Écran « Photo » : prendre une photo (enregistrée sur la carte SD du module) et la voir. */
@Composable
fun EcranCamera(modifier: Modifier, client: MotoBleClient) {
    val etat by client.etatModule.collectAsStateWithLifecycle()
    val telechargement by client.telechargement.collectAsStateWithLifecycle()
    var enAttente by remember { mutableStateOf(false) }
    var dernierePhoto by remember { mutableStateOf<Evenement.Photo?>(null) }
    var image by remember { mutableStateOf<ImageBitmap?>(null) }

    // Quand le module annonce la photo, on la télécharge pour l'afficher.
    LaunchedEffect(Unit) {
        client.evenements.collect { evenement ->
            if (evenement is Evenement.Photo) {
                enAttente = false
                dernierePhoto = evenement
                if (evenement.ok) {
                    image = null
                    client.telecharger(evenement.chemin)?.let { octets ->
                        image = BitmapFactory.decodeByteArray(octets, 0, octets.size)?.asImageBitmap()
                    }
                }
            }
        }
    }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Carte("Caméra") {
            Ligne("État", etat.carte?.camera ?: "inconnu")
            Button(
                onClick = { enAttente = true; client.envoyerCommande("PHOTO") },
                enabled = !enAttente && telechargement !is EtatTelechargement.EnCours,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.CameraAlt, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Prendre une photo")
            }
            Text(
                "La photo est enregistrée sur la carte SD du module, dans un dossier par jour : " +
                    "/photos/jj-mm-aaaa/jj-mm-aaaa_hh-mm-ss_ESP32-A7670.jpg",
                style = MaterialTheme.typography.bodySmall,
            )
            if (enAttente) LinearProgressIndicator(Modifier.fillMaxWidth())
        }

        dernierePhoto?.let { photo ->
            Carte("Dernière photo") {
                if (!photo.ok) {
                    Text("⚠️ ${photo.message}", color = MaterialTheme.colorScheme.error)
                } else {
                    Text(photo.chemin, style = MaterialTheme.typography.bodySmall)
                    Ligne("Taille", Protocole.taille(photo.taille))
                    val t = telechargement
                    if (t is EtatTelechargement.EnCours) {
                        Text("Transfert vers le téléphone…")
                        LinearProgressIndicator(
                            progress = { if (t.total > 0) t.recus.toFloat() / t.total else 0f },
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                    image?.let {
                        Image(it, contentDescription = "Photo", contentScale = ContentScale.Fit,
                            modifier = Modifier.fillMaxWidth().heightIn(max = 420.dp))
                    }
                    Text("Pour l'enregistrer sur le téléphone : écran Fichiers, dossier /photos.", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}
