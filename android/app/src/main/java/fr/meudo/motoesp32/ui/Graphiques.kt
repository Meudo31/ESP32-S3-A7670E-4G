package fr.meudo.motoesp32.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.cos
import kotlin.math.sin

/*
 * Petits graphiques dessinés à la main (Canvas), réutilisés sur tous les écrans :
 *   - JaugeCirculaire : compteur en arc de cercle (vitesse, batterie, signal)
 *   - Boussole        : cap de la moto
 *   - BarresHorizontales : comparaison de quelques valeurs (satellites, mémoire...)
 *   - Courbe          : historique d'une valeur dans le temps
 *   - BarresDeSignal  : les 5 barres du réseau mobile
 */

/** Couleur qui va du rouge (0) au vert (1) en passant par l'orange. */
fun couleurSelonLeNiveau(niveau: Float): Color = when {
    niveau < 0.2f -> Color(0xFFEF4444)
    niveau < 0.5f -> Color(0xFFF59E0B)
    else -> Color(0xFF22C55E)
}

@Composable
fun JaugeCirculaire(
    valeur: Float,
    maximum: Float,
    texte: String,
    unite: String,
    modifier: Modifier = Modifier,
    taille: Dp = 140.dp,
    couleur: Color = MaterialTheme.colorScheme.primary,
) {
    val fond = MaterialTheme.colorScheme.surfaceVariant
    val proportion = if (maximum > 0) (valeur / maximum).coerceIn(0f, 1f) else 0f
    Box(modifier.size(taille), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(taille)) {
            val epaisseur = size.minDimension * 0.1f
            val zone = Size(size.width - epaisseur, size.height - epaisseur)
            val coin = Offset(epaisseur / 2, epaisseur / 2)
            drawArc(fond, 135f, 270f, false, coin, zone, style = Stroke(epaisseur, cap = StrokeCap.Round))
            drawArc(couleur, 135f, 270f * proportion, false, coin, zone, style = Stroke(epaisseur, cap = StrokeCap.Round))
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(texte, style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
            Text(unite, style = MaterialTheme.typography.labelMedium)
        }
    }
}

@Composable
fun Boussole(capDegres: Float, texte: String, modifier: Modifier = Modifier, taille: Dp = 140.dp) {
    val couleurCadran = MaterialTheme.colorScheme.onSurfaceVariant
    val couleurAiguille = MaterialTheme.colorScheme.primary
    val fond = MaterialTheme.colorScheme.surfaceVariant
    Box(modifier.size(taille), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(taille)) {
            val rayon = size.minDimension / 2
            drawCircle(fond, rayon)
            // Graduations tous les 30°, plus longues aux points cardinaux
            for (angle in 0 until 360 step 30) {
                val radians = Math.toRadians(angle.toDouble() - 90)
                val longueur = if (angle % 90 == 0) rayon * 0.18f else rayon * 0.08f
                val exterieur = Offset(center.x + (rayon * 0.95f * cos(radians)).toFloat(), center.y + (rayon * 0.95f * sin(radians)).toFloat())
                val interieur = Offset(center.x + ((rayon * 0.95f - longueur) * cos(radians)).toFloat(), center.y + ((rayon * 0.95f - longueur) * sin(radians)).toFloat())
                drawLine(couleurCadran, interieur, exterieur, strokeWidth = 3f)
            }
            rotate(capDegres) {
                val fleche = Path().apply {
                    moveTo(center.x, center.y - rayon * 0.7f)
                    lineTo(center.x + rayon * 0.15f, center.y + rayon * 0.2f)
                    lineTo(center.x, center.y + rayon * 0.05f)
                    lineTo(center.x - rayon * 0.15f, center.y + rayon * 0.2f)
                    close()
                }
                drawPath(fleche, couleurAiguille)
            }
        }
        Text("N", Modifier.align(Alignment.TopCenter), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
        Text(texte, Modifier.align(Alignment.BottomCenter), style = MaterialTheme.typography.labelMedium)
    }
}

data class Barre(val libelle: String, val valeur: Float, val texte: String, val couleur: Color? = null)

@Composable
fun BarresHorizontales(barres: List<Barre>, maximum: Float, modifier: Modifier = Modifier) {
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        barres.forEach { barre ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(barre.libelle, Modifier.width(96.dp), style = MaterialTheme.typography.bodySmall)
                LinearProgressIndicator(
                    progress = { if (maximum > 0) (barre.valeur / maximum).coerceIn(0f, 1f) else 0f },
                    modifier = Modifier.weight(1f).height(10.dp),
                    color = barre.couleur ?: MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.surfaceVariant,
                    drawStopIndicator = {},
                )
                Text(barre.texte, Modifier.width(72.dp), style = MaterialTheme.typography.bodySmall, textAlign = TextAlign.End)
            }
        }
    }
}

/** Courbe d'une série de valeurs, avec le minimum et le maximum affichés. */
@Composable
fun Courbe(valeurs: List<Float>, unite: String, modifier: Modifier = Modifier, couleur: Color = MaterialTheme.colorScheme.primary) {
    val grille = MaterialTheme.colorScheme.surfaceVariant
    if (valeurs.size < 2) {
        Text("Pas encore assez de mesures pour tracer la courbe.", style = MaterialTheme.typography.bodySmall)
        return
    }
    val minimum = valeurs.min()
    val maximum = valeurs.max()
    val ecart = (maximum - minimum).takeIf { it > 0.0001f } ?: 1f
    Column(modifier.fillMaxWidth()) {
        Text("max %.1f %s".format(maximum, unite), style = MaterialTheme.typography.labelSmall)
        Canvas(Modifier.fillMaxWidth().height(110.dp)) {
            for (ligne in 0..3) {
                val y = size.height * ligne / 3
                drawLine(grille, Offset(0f, y), Offset(size.width, y), strokeWidth = 1f)
            }
            val trace = Path()
            valeurs.forEachIndexed { index, valeur ->
                val x = size.width * index / (valeurs.size - 1)
                val y = size.height - (valeur - minimum) / ecart * size.height
                if (index == 0) trace.moveTo(x, y) else trace.lineTo(x, y)
            }
            drawPath(trace, couleur, style = Stroke(4f, cap = StrokeCap.Round))
        }
        Text("min %.1f %s".format(minimum, unite), style = MaterialTheme.typography.labelSmall)
    }
}

@Composable
fun BarresDeSignal(pourcentage: Int, modifier: Modifier = Modifier) {
    val allumee = couleurSelonLeNiveau(pourcentage / 100f)
    val eteinte = MaterialTheme.colorScheme.surfaceVariant
    val barresAllumees = when {
        pourcentage <= 0 -> 0
        pourcentage < 20 -> 1
        pourcentage < 40 -> 2
        pourcentage < 60 -> 3
        pourcentage < 80 -> 4
        else -> 5
    }
    Canvas(modifier.size(width = 60.dp, height = 36.dp)) {
        val largeur = size.width / 5
        for (numero in 0 until 5) {
            val hauteur = size.height * (numero + 1) / 5
            drawRect(
                if (numero < barresAllumees) allumee else eteinte,
                topLeft = Offset(numero * largeur + 2, size.height - hauteur),
                size = Size(largeur - 4, hauteur),
            )
        }
    }
}
