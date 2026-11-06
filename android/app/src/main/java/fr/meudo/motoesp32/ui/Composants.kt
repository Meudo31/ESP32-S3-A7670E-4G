package fr.meudo.motoesp32.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

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

/** Ligne « libellé ........ valeur ». */
@Composable
fun Ligne(libelle: String, valeur: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(libelle, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        Text(valeur, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium, textAlign = TextAlign.End,
            modifier = Modifier.weight(1f))
    }
}
