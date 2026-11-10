package fr.meudo.motoesp32.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowLeft
import androidx.compose.material.icons.filled.ArrowRight
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.ErrorOutline
import androidx.compose.material.icons.filled.Highlight
import androidx.compose.material.icons.filled.Lightbulb
import androidx.compose.material.icons.filled.PanTool
import androidx.compose.material.icons.filled.Report
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.TwoWheeler
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import fr.meudo.motoesp32.ble.ValeursCan
import kotlin.math.cos
import kotlin.math.sin

/*
 * Tableau de bord façon moto, dessiné à partir des valeurs décodées du bus CAN.
 * Tous les éléments dont on connaît la trame sont toujours affichés : sans donnée,
 * ils restent éteints (voyants) ou affichent « — » (cadrans et cases).
 */

private val FOND = Color(0xFF0B0F14)
private val CADRAN = Color(0xFF151B23)
private val GRADUATION = Color(0xFFCBD5E1)
private val ETEINT = Color(0xFF2A323C)
private val TEXTE_ETEINT = Color(0xFF5B6673)
private val TEXTE = Color(0xFFE2E8F0)
private val VERT = Color(0xFF22C55E)
private val BLEU = Color(0xFF3B82F6)
private val ORANGE = Color(0xFFF59E0B)
private val ROUGE = Color(0xFFEF4444)
private val AIGUILLE = Color(0xFFF97316)

private const val VITESSE_MAX = 220f
private const val REGIME_MAX = 9000f
private const val ZONE_ROUGE = 8000f

@Composable
fun TableauDeBordCan(v: ValeursCan, actif: Boolean) {
    Column(
        Modifier.fillMaxWidth().background(FOND, RoundedCornerShape(20.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Voyants(v)
        Row(verticalAlignment = Alignment.CenterVertically) {
            JaugeVerticale("E", "F", v.carburant?.toFloat()?.div(100f), v.carburant?.let { "%.0f %%".format(it) },
                couleurDe = { if (it < 0.15f) ROUGE else if (it < 0.3f) ORANGE else VERT }, symbole = "⛽")
            CadranPrincipal(v, actif, Modifier.weight(1f).padding(horizontal = 8.dp))
            JaugeVerticale("C", "H", v.temperatureMoteur?.let { ((it - 40) / 100).toFloat() },
                v.temperatureMoteur?.let { "%.0f °C".format(it) },
                couleurDe = { if (it > 0.85f) ROUGE else if (it > 0.7f) ORANGE else BLEU }, symbole = "🌡")
        }
        BarreHorizontale("Gaz", v.papillon?.toFloat()?.div(100f), v.papillon?.let { "%.0f %%".format(it) }, VERT)
        BarreHorizontale("Couple", v.couple?.toFloat()?.div(100f), v.couple?.let { "%.0f %%".format(it) }, AIGUILLE)
        Cases(v)
        if (!actif) Text(
            "Bus CAN muet : les valeurs s'afficheront dès que la moto enverra ses trames.",
            color = TEXTE_ETEINT, fontSize = 12.sp, modifier = Modifier.fillMaxWidth(), textAlign = TextAlign.Center,
        )
    }
}

// ─────────────────────────────── Voyants ───────────────────────────────

private data class Voyant(val nom: String, val icone: ImageVector, val couleur: Color, val allume: Boolean?)

@Composable
private fun Voyants(v: ValeursCan) {
    val lampes = v.lampes
    fun lampe(bit: Int) = lampes?.let { it and bit != 0 }
    val clignoG = if (v.warning == true) true else v.clignotantGauche
    val clignoD = if (v.warning == true) true else v.clignotantDroit
    val haut = listOf(
        Voyant("Gauche", Icons.Filled.ArrowLeft, VERT, clignoG),
        Voyant("Croisement", Icons.Filled.Lightbulb, VERT, v.feuCroisement),
        Voyant("Route", Icons.Filled.Highlight, BLEU, v.feuRoute),
        Voyant("Régulateur", Icons.Filled.Speed, VERT, v.regulateur),
        Voyant("Warning", Icons.Filled.Warning, ORANGE, v.warning),
        Voyant("Droite", Icons.Filled.ArrowRight, VERT, clignoD),
    )
    val bas = listOf(
        Voyant("Moteur", Icons.Filled.Build, ORANGE, lampe(1)),
        Voyant("Stop", Icons.Filled.Report, ROUGE, lampe(2)),
        Voyant("Alerte", Icons.Filled.ErrorOutline, ORANGE, lampe(4)),
        Voyant("Protection", Icons.Filled.Shield, ORANGE, lampe(8)),
        Voyant("Béquille", Icons.Filled.TwoWheeler, ORANGE, v.bequille),
        Voyant("Coupe-circuit", Icons.Filled.Block, ROUGE, v.coupeCircuitSurRun?.not()),
        Voyant("Frein", Icons.Filled.Report, ROUGE, v.frein),
        Voyant("Embrayage", Icons.Filled.PanTool, VERT, v.embrayage),
    )
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        RangeeDeVoyants(haut)
        RangeeDeVoyants(bas)
    }
}

@Composable
private fun RangeeDeVoyants(voyants: List<Voyant>) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
        voyants.forEach { voyant ->
            val allume = voyant.allume == true
            Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                Box(
                    Modifier.size(32.dp)
                        .background(if (allume) voyant.couleur.copy(alpha = 0.18f) else Color.Transparent, RoundedCornerShape(8.dp))
                        .border(1.dp, if (allume) voyant.couleur else ETEINT, RoundedCornerShape(8.dp)),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(voyant.icone, voyant.nom, tint = if (allume) voyant.couleur else ETEINT, modifier = Modifier.size(22.dp))
                }
                Text(
                    voyant.nom + if (voyant.allume == null) " ?" else "",
                    color = if (allume) TEXTE else TEXTE_ETEINT, fontSize = 9.sp, maxLines = 1, textAlign = TextAlign.Center,
                )
            }
        }
    }
}

// ─────────────────────────────── Cadran principal ───────────────────────────────

/** Compteur de vitesse à aiguille, compte-tours en arc intérieur, rapport engagé au centre. */
@Composable
private fun CadranPrincipal(v: ValeursCan, actif: Boolean, modifier: Modifier) {
    val mesureur = rememberTextMeasurer()
    val vitesse = v.vitesse?.toFloat()
    val regime = v.regime?.toFloat()
    Box(modifier.aspectRatio(1f), contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxWidth().aspectRatio(1f)) {
            val rayon = size.minDimension / 2
            val debut = 135f
            val balayage = 270f
            drawCircle(Brush.radialGradient(listOf(CADRAN, FOND), center, rayon), rayon)
            drawCircle(ETEINT, rayon, style = Stroke(rayon * 0.02f))

            // Graduations de vitesse : grande tous les 20 km/h avec le chiffre, petite tous les 10
            for (kmh in 0..VITESSE_MAX.toInt() step 10) {
                val angle = Math.toRadians((debut + balayage * kmh / VITESSE_MAX).toDouble())
                val grande = kmh % 20 == 0
                val r1 = rayon * 0.93f
                val r2 = rayon * if (grande) 0.82f else 0.87f
                drawLine(
                    GRADUATION,
                    Offset(center.x + r1 * cos(angle).toFloat(), center.y + r1 * sin(angle).toFloat()),
                    Offset(center.x + r2 * cos(angle).toFloat(), center.y + r2 * sin(angle).toFloat()),
                    strokeWidth = if (grande) rayon * 0.018f else rayon * 0.009f,
                )
                if (grande) {
                    val texte = mesureur.measure(kmh.toString(), TextStyle(color = GRADUATION, fontSize = (rayon * 0.075f).toSp()))
                    val rt = rayon * 0.71f
                    drawText(texte, topLeft = Offset(
                        center.x + rt * cos(angle).toFloat() - texte.size.width / 2,
                        center.y + rt * sin(angle).toFloat() - texte.size.height / 2,
                    ))
                }
            }

            // Compte-tours : arc intérieur, zone rouge à la fin
            val ep = rayon * 0.06f
            val rArc = rayon * 0.58f
            val coin = Offset(center.x - rArc, center.y - rArc)
            val zone = Size(rArc * 2, rArc * 2)
            drawArc(ETEINT, debut, balayage, false, coin, zone, style = Stroke(ep, cap = StrokeCap.Butt))
            drawArc(ROUGE.copy(alpha = 0.35f), debut + balayage * ZONE_ROUGE / REGIME_MAX, balayage * (1 - ZONE_ROUGE / REGIME_MAX),
                false, coin, zone, style = Stroke(ep))
            if (regime != null) {
                val p = (regime / REGIME_MAX).coerceIn(0f, 1f)
                drawArc(
                    if (regime >= ZONE_ROUGE) ROUGE else if (regime > 6000) ORANGE else VERT,
                    debut, balayage * p, false, coin, zone, style = Stroke(ep),
                )
            }
            for (milliers in 0..9) {
                val angle = Math.toRadians((debut + balayage * milliers * 1000 / REGIME_MAX).toDouble())
                val texte = mesureur.measure(milliers.toString(), TextStyle(color = TEXTE_ETEINT, fontSize = (rayon * 0.055f).toSp()))
                val rt = rArc - ep * 1.4f
                drawText(texte, topLeft = Offset(
                    center.x + rt * cos(angle).toFloat() - texte.size.width / 2,
                    center.y + rt * sin(angle).toFloat() - texte.size.height / 2,
                ))
            }

            // Aiguille de vitesse
            val p = ((vitesse ?: 0f) / VITESSE_MAX).coerceIn(0f, 1f)
            val angle = Math.toRadians((debut + balayage * p).toDouble())
            drawLine(
                if (vitesse != null) AIGUILLE else ETEINT,
                Offset(center.x - rayon * 0.12f * cos(angle).toFloat(), center.y - rayon * 0.12f * sin(angle).toFloat()),
                Offset(center.x + rayon * 0.88f * cos(angle).toFloat(), center.y + rayon * 0.88f * sin(angle).toFloat()),
                strokeWidth = rayon * 0.025f, cap = StrokeCap.Round,
            )
            drawCircle(if (vitesse != null) AIGUILLE else ETEINT, rayon * 0.05f)
        }
        // Affichage numérique au centre bas
        Column(Modifier.align(Alignment.BottomCenter).padding(bottom = 4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Row(verticalAlignment = Alignment.Bottom) {
                Text(vitesse?.let { "%.0f".format(it) } ?: "—", color = if (vitesse != null) TEXTE else TEXTE_ETEINT,
                    fontSize = 30.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                Text(" km/h", color = TEXTE_ETEINT, fontSize = 11.sp, modifier = Modifier.padding(bottom = 6.dp))
            }
            Text(regime?.let { "%.0f tr/min".format(it) } ?: "— tr/min", color = if (regime != null) TEXTE else TEXTE_ETEINT, fontSize = 11.sp)
        }
        // Rapport engagé
        Box(
            Modifier.align(Alignment.Center).size(44.dp)
                .background(CADRAN, RoundedCornerShape(8.dp))
                .border(2.dp, if (v.rapport != null) VERT else ETEINT, RoundedCornerShape(8.dp)),
            contentAlignment = Alignment.Center,
        ) {
            Text(v.rapport ?: "–", color = if (v.rapport == "N") VERT else if (v.rapport != null) TEXTE else TEXTE_ETEINT,
                fontSize = 26.sp, fontWeight = FontWeight.Bold)
        }
    }
}

// ─────────────────────────────── Jauges ───────────────────────────────

@Composable
private fun JaugeVerticale(bas: String, haut: String, proportion: Float?, texte: String?, couleurDe: (Float) -> Color, symbole: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(44.dp)) {
        Text(haut, color = TEXTE_ETEINT, fontSize = 10.sp)
        Box(Modifier.width(14.dp).height(130.dp).background(ETEINT, RoundedCornerShape(7.dp)), contentAlignment = Alignment.BottomCenter) {
            if (proportion != null) {
                val p = proportion.coerceIn(0f, 1f)
                Box(Modifier.width(14.dp).height((130 * p).dp).background(couleurDe(p), RoundedCornerShape(7.dp)))
            }
        }
        Text(bas, color = TEXTE_ETEINT, fontSize = 10.sp)
        Text(symbole, fontSize = 14.sp)
        Text(texte ?: "—", color = if (texte != null) TEXTE else TEXTE_ETEINT, fontSize = 11.sp, maxLines = 1)
    }
}

@Composable
private fun BarreHorizontale(nom: String, proportion: Float?, texte: String?, couleur: Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(nom, color = TEXTE_ETEINT, fontSize = 12.sp, modifier = Modifier.width(56.dp))
        Box(Modifier.weight(1f).height(10.dp).background(ETEINT, RoundedCornerShape(5.dp))) {
            if (proportion != null) Box(
                Modifier.fillMaxWidth(proportion.coerceIn(0f, 1f)).height(10.dp).background(couleur, RoundedCornerShape(5.dp)),
            )
        }
        Spacer(Modifier.width(8.dp))
        Text(texte ?: "—", color = if (texte != null) TEXTE else TEXTE_ETEINT, fontSize = 12.sp,
            modifier = Modifier.width(48.dp), textAlign = TextAlign.End)
    }
}

// ─────────────────────────────── Cases chiffrées ───────────────────────────────

@Composable
private fun Cases(v: ValeursCan) {
    fun f(x: Double?, motif: String) = x?.let { motif.format(it) }
    fun pneu(p: Double?, t: Double?) = p?.let { "%.2f b".format(it) + (t?.let { " · %.0f°".format(it) } ?: "") }
    val cases = listOf(
        "Compteur total" to f(v.odometre, "%.1f km"),
        "Trip" to f(v.trip, "%.1f km"),
        "Autonomie" to f(v.autonomieKm, "%.0f km"),
        "Conso instant." to f(v.consoInstantanee, "%.1f L/100"),
        "Conso moyenne" to f(v.consoMoyenne, "%.1f L/100"),
        "Débit essence" to f(v.debitCarburant, "%.2f L/h"),
        "Batterie moto" to f(v.tensionBatterie, "%.2f V"),
        "Temp. extérieure" to f(v.temperatureExterieure, "%.1f °C"),
        "Roue avant" to f(v.vitesseAvant, "%.0f km/h"),
        "Patinage" to f(v.patinage, "%+.1f km/h"),
        "Pneu avant" to pneu(v.pressionAvant, v.temperaturePneuAvant),
        "Pneu arrière" to pneu(v.pressionArriere, v.temperaturePneuArriere),
        "Capteur de chute" to v.capteurDeChute?.let { "$it" + if (it < 118) " (béq.)" else " (droit)" },
        "Diagnostic OBD" to if (v.obd) "vu" else null,
    )
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        cases.chunked(3).forEach { rangee ->
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                rangee.forEach { (nom, valeur) ->
                    Column(
                        Modifier.weight(1f).background(CADRAN, RoundedCornerShape(10.dp)).padding(horizontal = 8.dp, vertical = 6.dp),
                    ) {
                        Text(nom, color = TEXTE_ETEINT, fontSize = 10.sp, maxLines = 1)
                        Text(valeur ?: "—", color = if (valeur != null) TEXTE else TEXTE_ETEINT, fontSize = 14.sp,
                            fontWeight = FontWeight.SemiBold, fontFamily = FontFamily.Monospace, maxLines = 1)
                    }
                }
                repeat(3 - rangee.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}
