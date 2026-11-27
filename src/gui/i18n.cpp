#include "zoal_atc/gui/i18n.hpp"

#include <cstring>
#include <string_view>
#include <unordered_map>

namespace zoal_atc::gui {

namespace {

struct Row {
  const char *en;
  const char *es;
  const char *fr;
};

// Every string the panel and the HUD write, in English, Spanish and French.
// The English is the key and must match the source exactly. A row ending in a
// space is a prefix: "Request " translates "Request FL230" and keeps the rest.
const Row kRows[] = {
    // The mast and the tabs.
    {"Console ", "Consola ", "Console "},
    {"Radio Panel", "Panel de radio", "Panneau radio"},
    {"Home", "Inicio", "Accueil"},
    {"Settings", "Ajustes", "Réglages"},
    {"connected", "conectada", "connectée"},
    {"disconnected", "desconectada", "déconnectée"},
    {"subscribing", "suscribiendo", "abonnement"},
    {"connected (", "conectada (", "connectée ("},
    {" in flight)", " en curso)", " en cours)"},
    // Home.
    {"to ", "a ", "à "},
    {"No transmissions yet", "Aún no hay transmisiones", "Aucune transmission pour l'instant"},
    {"Hold push-to-talk, or type a transmission below.",
     "Mantenga pulsado el botón de transmisión o escriba una transmisión abajo.",
     "Maintenez l'alternat, ou tapez une transmission ci-dessous."},
    {"1 new transmission", "1 transmisión nueva", "1 nouvelle transmission"},
    {" new transmissions", " transmisiones nuevas", " nouvelles transmissions"},
    {"Confirm", "Confirmar", "Confirmer"},
    {"New flight", "Nuevo vuelo", "Nouveau vol"},
    {"Reload", "Recargar", "Recharger"},
    {"Flight", "Vuelo", "Vol"},
    {"Altitude", "Altitud", "Altitude"},
    {"Traffic", "Tránsito", "Trafic"},
    {"Facility", "Dependencia", "Organisme"},
    {"Filed plan", "Plan presentado", "Plan déposé"},
    {"waiting", "esperando", "en attente"},
    {"Radio", "Radio", "Radio"},
    {"Tune", "Sintonizar", "Régler"},
    {"Send", "Enviar", "Envoyer"},
    {"Type a transmission\xE2\x80\xA6", "Escriba una transmisión\xE2\x80\xA6", "Tapez une transmission\xE2\x80\xA6"},
    {"sby ", "res ", "att "},
    {"on ground", "en tierra", "au sol"},
    {" contacts", " contactos", " contacts"},
    {"unavailable", "no disponible", "indisponible"},
    {"1 aircraft", "1 aeronave", "1 appareil"},
    {" aircraft", " aeronaves", " appareils"},
    {"filed", "presentado", "déposé"},
    {"first ", "primero ", "premier "},
    {" updates dropped", " actualizaciones perdidas", " mises à jour perdues"},
    {"Console unreachable \xE2\x80\x94 values are last known",
     "Consola inaccesible \xE2\x80\x94 valores de la última conexión",
     "Console injoignable \xE2\x80\x94 dernières valeurs connues"},
    {"Simulator paused", "Simulador en pausa", "Simulateur en pause"},
    {"No telemetry for ", "Sin telemetría desde hace ", "Pas de télémétrie depuis "},
    {"the panel is not attached to the plugin", "el panel no está conectado al plugin",
     "le panneau n'est pas relié au plugin"},
    {"the request was refused", "la solicitud fue rechazada", "la demande a été refusée"},
    // Settings.
    {"Reply language", "Idioma de las respuestas", "Langue des réponses"},
    {"Controllers answer you in this language. Push-to-talk and typed calls are still English only: "
     "speak and type to them in English. Changing it reconnects to the console.",
     "Los controladores le responden en este idioma. El botón de transmisión y las llamadas escritas "
     "siguen siendo solo en inglés: hábleles y escríbales en inglés. Cambiarlo vuelve a conectar con la consola.",
     "Les contrôleurs vous répondent dans cette langue. L'alternat et les messages tapés restent en "
     "anglais uniquement : parlez-leur et écrivez-leur en anglais. La changer reconnecte la console."},
    {"My voice", "Mi voz", "Ma voix"},
    {"Previous", "Anterior", "Précédente"},
    {"Next", "Siguiente", "Suivante"},
    {"A call you make with the HUD's buttons is spoken in this voice on your radio, so you hear "
     "yourself call. It is never the voice the controller on that frequency speaks in.",
     "Una llamada hecha con los botones del HUD se dice con esta voz en su radio, para que se oiga "
     "llamar. Nunca es la voz del controlador de esa frecuencia.",
     "Un appel fait avec les boutons du HUD est dit avec cette voix sur votre radio, pour que vous "
     "vous entendiez appeler. Ce n'est jamais la voix du contrôleur de cette fréquence."},
    {"American", "estadounidense", "américain"},
    {"British", "británico", "britannique"},
    {"Spanish", "español", "espagnol"},
    {"French", "francés", "français"},
    {"Hindi", "hindi", "hindi"},
    {"Italian", "italiano", "italien"},
    {"Japanese", "japonés", "japonais"},
    {"Portuguese", "portugués", "portugais"},
    {"Mandarin", "mandarín", "mandarin"},
    {" female", " femenina", " féminine"},
    {" male", " masculina", " masculine"},
    {"default", "predeterminada", "par défaut"},
    {"Auto readback", "Colación automática", "Collationnement automatique"},
    {"On", "Sí", "Oui"},
    {"Off", "No", "Non"},
    {"Reads back a clearance or instruction for you, with the readback button, once the transmission "
     "has finished playing on your radio. Off, you press the readback yourself.",
     "Colaciona por usted una autorización o instrucción, con el botón de colación, cuando la "
     "transmisión ha terminado de oírse en su radio. Desactivado, pulsa usted la colación.",
     "Collationne pour vous une clairance ou une instruction, avec le bouton de collationnement, une "
     "fois la transmission terminée sur votre radio. Désactivé, vous appuyez vous-même."},
    {"Typing line on the HUD", "Línea de escritura en el HUD", "Ligne de saisie sur le HUD"},
    {"A line under the HUD's bar to type a call in and send with Enter, without opening this panel. "
     "Off, the HUD shows only its buttons.",
     "Una línea bajo la barra del HUD para escribir una llamada y enviarla con Intro, sin abrir este "
     "panel. Desactivado, el HUD muestra solo sus botones.",
     "Une ligne sous la barre du HUD pour taper un appel et l'envoyer avec Entrée, sans ouvrir ce "
     "panneau. Désactivé, le HUD n'affiche que ses boutons."},
    {"Transcript on the HUD", "Transcripción en el HUD", "Transcription sur le HUD"},
    {"The last few transmissions under the HUD's buttons. Off, you hear them on the radio and read "
     "them in this panel's radio log.",
     "Las últimas transmisiones bajo los botones del HUD. Desactivado, las oye en la radio y las lee "
     "en el registro de radio de este panel.",
     "Les dernières transmissions sous les boutons du HUD. Désactivé, vous les entendez à la radio et "
     "les lisez dans le journal radio de ce panneau."},
    {"Appearance", "Apariencia", "Apparence"},
    {"Theme", "Tema", "Thème"},
    {"Graphite", "Grafito", "Graphite"},
    {"Warm dark glass. The default.", "Cristal oscuro cálido. El predeterminado.",
     "Verre sombre et chaud. Par défaut."},
    {"Daylight", "Luz de día", "Jour"},
    {"Light glass for a bright cockpit.", "Cristal claro para una cabina luminosa.",
     "Verre clair pour un cockpit lumineux."},
    {"Night", "Noche", "Nuit"},
    {"Dim and low-glare, for flying in the dark.", "Tenue y sin reflejos, para volar de noche.",
     "Atténué et sans éblouissement, pour voler de nuit."},
    {"Applies to this panel and the HUD, and is remembered for this aircraft.",
     "Se aplica a este panel y al HUD, y se recuerda para esta aeronave.",
     "S'applique à ce panneau et au HUD, et est mémorisé pour cet appareil."},
    {"Calls by button", "Llamadas con botones", "Appels par bouton"},
    {"Connection", "Conexión", "Connexion"},
    {"Console", "Consola", "Console"},
    {"Where this plugin connects. Fixed here \xE2\x80\x94 change it in Output/preferences/zoal_atc.cfg "
     "if you run your own console.",
     "Adónde se conecta este plugin. Fijo aquí \xE2\x80\x94 cámbielo en Output/preferences/zoal_atc.cfg "
     "si usa su propia consola.",
     "Où ce plugin se connecte. Fixé ici \xE2\x80\x94 modifiez-le dans Output/preferences/zoal_atc.cfg "
     "si vous utilisez votre propre console."},
    {"Access token", "Token de acceso", "Jeton d'accès"},
    {"Leave empty if the console is open", "Déjelo vacío si la consola es abierta",
     "Laissez vide si la console est ouverte"},
    {"Hide", "Ocultar", "Masquer"},
    {"Show", "Mostrar", "Afficher"},
    {"Save token", "Guardar token", "Enregistrer le jeton"},
    {"Authorises this aircraft with the console. Saving reconnects, so the status above tells you "
     "straight away whether it was accepted. Unlike everything else here it is stored by the plugin, "
     "not the console \xE2\x80\x94 it has to work while the console is unreachable, because it is how "
     "it becomes reachable. Save it empty to clear it.",
     "Autoriza esta aeronave ante la consola. Guardarlo vuelve a conectar, así que el estado de arriba "
     "le dice enseguida si se aceptó. A diferencia de todo lo demás, lo guarda el plugin y no la "
     "consola \xE2\x80\x94 debe funcionar cuando la consola es inaccesible, porque es lo que la hace "
     "accesible. Guárdelo vacío para borrarlo.",
     "Autorise cet appareil auprès de la console. L'enregistrer reconnecte, donc l'état ci-dessus vous "
     "dit tout de suite s'il a été accepté. Contrairement au reste, il est conservé par le plugin et "
     "non par la console \xE2\x80\x94 il doit fonctionner quand la console est injoignable, car c'est "
     "ce qui la rend joignable. Enregistrez-le vide pour l'effacer."},
    {"Flight plan", "Plan de vuelo", "Plan de vol"},
    {"SimBrief pilot ID", "ID de piloto de SimBrief", "Identifiant pilote SimBrief"},
    {"Pilot ID", "ID de piloto", "Identifiant pilote"},
    {"Save", "Guardar", "Enregistrer"},
    {"Remembered for this aircraft, so an import needs no ID. Two pilots connected to one console "
     "each keep their own.",
     "Se recuerda para esta aeronave, así que importar no necesita ID. Dos pilotos conectados a una "
     "consola guardan cada uno el suyo.",
     "Mémorisé pour cet appareil, donc l'import n'a pas besoin d'identifiant. Deux pilotes connectés à "
     "une console gardent chacun le leur."},
    {"Imported plan", "Plan importado", "Plan importé"},
    {"No plan imported yet.", "Aún no se ha importado ningún plan.", "Aucun plan importé pour l'instant."},
    {"Import now", "Importar ahora", "Importer maintenant"},
    {"Logbook", "Registro de vuelo", "Carnet de vol"},
    {"Logs kept", "Registros guardados", "Journaux conservés"},
    {"All", "Todos", "Tous"},
    {"Every transmission of a flight is written to the logbook folder inside the zoal-atc plugin "
     "folder, one file per callsign and day, in English and in the reply language. Only the newest "
     "files are kept; past 100 keeps them all.",
     "Cada transmisión de un vuelo se escribe en la carpeta logbook dentro de la carpeta del plugin "
     "zoal-atc, un archivo por indicativo y día, en inglés y en el idioma de las respuestas. Solo se "
     "guardan los archivos más recientes; por encima de 100 se guardan todos.",
     "Chaque transmission d'un vol est écrite dans le dossier logbook du dossier du plugin zoal-atc, "
     "un fichier par indicatif et par jour, en anglais et dans la langue des réponses. Seuls les "
     "fichiers les plus récents sont conservés ; au-delà de 100, tous le sont."},
    // The HUD.
    {"Plan reloaded", "Plan recargado", "Plan rechargé"},
    {"Reload plan", "Recargar plan", "Recharger le plan"},
    {"Started", "Iniciado", "Commencé"},
    {"Open panel", "Abrir panel", "Ouvrir le panneau"},
    {"Type a call, Enter to send", "Escriba una llamada, Intro para enviar", "Tapez un appel, Entrée pour envoyer"},
    {"CONSOLE UNREACHABLE", "CONSOLA INACCESIBLE", "CONSOLE INJOIGNABLE"},
    {"overheard", "escuchado", "entendu"},
    {"Aircraft", "Aeronave", "Appareil"},
    {"You", "Usted", "Vous"},
    {"the console refused", "la consola rechazó", "la console a refusé"},
    {"the call could not be sent", "no se pudo enviar la llamada", "l'appel n'a pas pu être envoyé"},
    {"the message could not be sent", "no se pudo enviar el mensaje", "le message n'a pas pu être envoyé"},
    {"the radio could not be tuned", "no se pudo sintonizar la radio", "la radio n'a pas pu être réglée"},
    {"the HUD is not attached to the console", "el HUD no está conectado a la consola",
     "le HUD n'est pas relié à la console"},
    {"the console refused the call", "la consola rechazó la llamada", "la console a refusé l'appel"},
    // The positions, as the HUD names who said a line.
    {"Clearance Delivery", "Autorizaciones", "Prévol"},
    {"Ground", "Rodadura", "Sol"},
    {"Tower", "Torre", "Tour"},
    {"Departure", "Salidas", "Départ"},
    {"Approach", "Aproximación", "Approche"},
    {"Center", "Control", "Contrôle"},
    {"Oceanic", "Oceánico", "Océanique"},
    {"Ramp", "Plataforma", "Piste de trafic"},
    // The calls the console offers as buttons. What a button says on the radio
    // is the console's English and is never translated; only its label is.
    {"Check in", "Contacto inicial", "Premier contact"},
    {"Radio check", "Prueba de radio", "Essai radio"},
    {"Read back", "Colacionar", "Collationner"},
    {"Say again", "Repita", "Répétez"},
    {"IFR clearance", "Autorización IFR", "Clairance IFR"},
    {"VFR departure", "Salida VFR", "Départ VFR"},
    {"No SID", "Sin SID", "Sans SID"},
    {"Request pushback", "Solicitar remolque", "Demander le repoussage"},
    {"Taxi to the gate", "Rodar al puesto", "Rouler au poste"},
    {"Clear of runway", "Pista libre", "Piste dégagée"},
    {"Ready for departure", "Listo para salir", "Prêt au départ"},
    {"Inbound for landing", "En aproximación para aterrizar", "En approche pour atterrir"},
    {"Going around", "Motor y al aire", "Remise des gaz"},
    {"Missed approach", "Aproximación frustrada", "Approche interrompue"},
    {"Position report", "Notificación de posición", "Compte rendu de position"},
    {"Altimeter?", "¿Altímetro?", "Calage ?"},
    {"Altitude?", "¿Altitud?", "Altitude ?"},
    {"Arrival?", "¿Llegada?", "Arrivée ?"},
    {"ATIS?", "¿ATIS?", "ATIS ?"},
    {"Ceiling?", "¿Techo?", "Plafond ?"},
    {"Departure freq?", "¿Frecuencia de salida?", "Fréquence départ ?"},
    {"Destination weather?", "¿Meteorología del destino?", "Météo à destination ?"},
    {"Ground freq?", "¿Frecuencia de rodadura?", "Fréquence sol ?"},
    {"Heading?", "¿Rumbo?", "Cap ?"},
    {"Hold short?", "¿Espera?", "Point d'arrêt ?"},
    {"Level?", "¿Nivel?", "Niveau ?"},
    {"Next fix?", "¿Próximo punto?", "Prochain point ?"},
    {"Runway?", "¿Pista?", "Piste ?"},
    {"Squawk?", "¿Transpondedor?", "Transpondeur ?"},
    {"Taxi route?", "¿Ruta de rodaje?", "Cheminement ?"},
    {"Tower freq?", "¿Frecuencia de torre?", "Fréquence tour ?"},
    {"Wind?", "¿Viento?", "Vent ?"},
    {"Request ", "Solicitar ", "Demander "},
    {"Holding short ", "En espera ", "Au point d'arrêt "},
    {"Landing ", "Aterrizaje ", "Atterrissage "},
    // The Traffic tab.
    {"Pilots", "Pilotos", "Pilotes"},
    {"AI", "IA", "IA"},
    {"Pilot", "Piloto", "Pilote"},
    {"Following", "Siguiendo", "Suivi caméra"},
    {"Tracking", "Seguimiento", "Pistage"},
    {"Distance", "Distancia", "Distance"},
    {"Bearing", "Rumbo", "Relèvement"},
    {"Relative", "Relativa", "Relative"},
    {"Speed", "Velocidad", "Vitesse"},
    {"Vertical", "Vertical", "Vertical"},
    {"Follow", "Seguir", "Suivre"},
    {"Stop following", "Dejar de seguir", "Arrêter le suivi"},
    {"Untrack", "Dejar de marcar", "Ne plus pister"},
    {"Callsign", "Indicativo", "Indicatif"},
    {"Type", "Tipo", "Type"},
    {"Dist", "Dist", "Dist"},
    {"Brg", "Rbo", "Rlv"},
    {"Alt", "Alt", "Alt"},
    {"Rel", "Rel", "Rel"},
    {"GS", "GS", "GS"},
    {"VS", "VS", "VS"},
    {"level", "nivelado", "palier"},
    {" aircraft", " aeronaves", " aéronefs"},
    {" pilots", " pilotos", " pilotes"},
    {"No traffic nearby", "No hay tráfico cerca", "Aucun trafic à proximité"},
    {"Nothing matches this filter", "Nada coincide con este filtro",
     "Rien ne correspond à ce filtre"},
    {"Aircraft the console sends appear here, nearest first.",
     "Las aeronaves que envía la consola aparecen aquí, la más cercana primero.",
     "Les aéronefs envoyés par la console apparaissent ici, le plus proche en premier."},
    {"Traffic is off", "El tráfico está desactivado", "Le trafic est désactivé"},
    {"Traffic is not running in this build.", "El tráfico no funciona en esta versión.",
     "Le trafic ne fonctionne pas dans cette version."},
    {"Traffic has not started.", "El tráfico no se ha iniciado.", "Le trafic n'a pas démarré."},
    {"No CSL models found, so traffic is off. Choose the folder that holds your CSL models "
     "below.",
     "No se encontraron modelos CSL, así que el tráfico está desactivado. Elija abajo la "
     "carpeta que contiene sus modelos CSL.",
     "Aucun modèle CSL trouvé, le trafic est donc désactivé. Choisissez ci-dessous le dossier "
     "qui contient vos modèles CSL."},
    {"Not on TCAS. It is held by ", "No aparece en el TCAS. Lo controla ",
     "Absent du TCAS. Il est contrôlé par "},
    {". Disable that plugin to see this traffic there.",
     ". Desactive ese plugin para ver este tráfico en él.",
     ". Désactivez ce plugin pour y voir ce trafic."},
    // The CSL folder picker.
    {"No CSL models found", "No se encontraron modelos CSL", "Aucun modèle CSL trouvé"},
    {"CSL models", "Modelos CSL", "Modèles CSL"},
    {"Done", "Listo", "Terminé"},
    {"In use", "En uso", "Utilisé"},
    {" models", " modelos", " modèles"},
    {"Found on this computer", "Encontradas en este equipo", "Trouvés sur cet ordinateur"},
    {"None under X-Plane's plugins folder.", "Ninguna en la carpeta de plugins de X-Plane.",
     "Aucun dans le dossier des plugins de X-Plane."},
    {"Use", "Usar", "Utiliser"},
    {"Another folder", "Otra carpeta", "Un autre dossier"},
    {"Browse\xE2\x80\xA6", "Examinar\xE2\x80\xA6", "Parcourir\xE2\x80\xA6"},
    {"Choosing\xE2\x80\xA6", "Eligiendo\xE2\x80\xA6", "Choix en cours\xE2\x80\xA6"},
    {"Path to a CSL folder", "Ruta a una carpeta CSL", "Chemin d'un dossier CSL"},
    {"Other pilots and the console's traffic are drawn with CSL aircraft models, and none were "
     "found where the plugin looks. Choose the folder that holds yours: a folder with aircraft "
     "packages in it, each with an xsb_aircraft.txt file.",
     "Los demás pilotos y el tráfico de la consola se dibujan con modelos de aeronave CSL, y no "
     "se encontró ninguno donde busca el plugin. Elija la carpeta que contiene los suyos: una "
     "carpeta con paquetes de aeronaves, cada uno con un archivo xsb_aircraft.txt.",
     "Les autres pilotes et le trafic de la console sont dessinés avec des modèles d'avions CSL, "
     "et aucun n'a été trouvé là où le plugin cherche. Choisissez le dossier qui contient les "
     "vôtres : un dossier de paquets d'avions, chacun avec un fichier xsb_aircraft.txt."},
    {"Other pilots and the console's traffic are drawn with these models. A new folder is used "
     "at once and remembered for the next flight.",
     "Los demás pilotos y el tráfico de la consola se dibujan con estos modelos. Una carpeta "
     "nueva se usa al instante y se recuerda para el próximo vuelo.",
     "Les autres pilotes et le trafic de la console sont dessinés avec ces modèles. Un nouveau "
     "dossier est utilisé aussitôt et retenu pour le prochain vol."},
    {"Type or choose a folder first.", "Escriba o elija una carpeta primero.",
     "Saisissez ou choisissez d'abord un dossier."},
    {"That folder has no CSL models: there is no xsb_aircraft.txt in it or below it.",
     "Esa carpeta no tiene modelos CSL: no hay ningún xsb_aircraft.txt en ella ni debajo.",
     "Ce dossier ne contient aucun modèle CSL : aucun xsb_aircraft.txt dedans ni en dessous."},
    {"Could not save it for next time: ", "No se pudo guardar para la próxima vez: ",
     "Impossible de l'enregistrer pour la prochaine fois : "},
    {"These models are already in use.", "Estos modelos ya están en uso.",
     "Ces modèles sont déjà utilisés."},
    {"Loaded. The previous folder's models stay until X-Plane restarts.",
     "Cargado. Los modelos de la carpeta anterior se mantienen hasta reiniciar X-Plane.",
     "Chargé. Les modèles du dossier précédent restent jusqu'au redémarrage de X-Plane."},
    {"The folder dialog is already open.", "El diálogo de carpetas ya está abierto.",
     "La fenêtre de choix de dossier est déjà ouverte."},
    {"the folder dialog could not be opened", "no se pudo abrir el diálogo de carpetas",
     "la fenêtre de choix de dossier n'a pas pu s'ouvrir"},
    {"no folder dialog is installed (zenity or kdialog); type the path instead",
     "no hay ningún diálogo de carpetas instalado (zenity o kdialog); escriba la ruta",
     "aucune fenêtre de choix de dossier n'est installée (zenity ou kdialog) ; saisissez le "
     "chemin"},
    {"Traffic could not start: ", "El tráfico no pudo iniciarse: ",
     "Le trafic n'a pas pu démarrer : "},
};

int language_column = 0; // 0 English, 1 Spanish, 2 French
std::string current_language = "en";

const std::unordered_map<std::string_view, const Row *> &rows() {
  static const std::unordered_map<std::string_view, const Row *> index = [] {
    std::unordered_map<std::string_view, const Row *> out;
    for (const Row &row : kRows) {
      out.emplace(row.en, &row);
    }
    return out;
  }();
  return index;
}

const char *column(const Row &row) {
  switch (language_column) {
  case 1:
    return row.es;
  case 2:
    return row.fr;
  default:
    return row.en;
  }
}

// composed holds the translations made by prefix, so tr can hand back a
// pointer that lives as long as the plugin.
std::unordered_map<std::string, std::string> &composed() {
  static std::unordered_map<std::string, std::string> cache;
  return cache;
}

} // namespace

void set_ui_language(const std::string &language) {
  if (language == "es") {
    language_column = 1;
  } else if (language == "fr") {
    language_column = 2;
  } else {
    language_column = 0;
  }
  current_language = language_column == 1 ? "es" : language_column == 2 ? "fr" : "en";
}

const std::string &ui_language() { return current_language; }

const char *tr(const char *english) {
  if (english == nullptr || language_column == 0) {
    return english;
  }
  const auto &index = rows();
  if (const auto found = index.find(english); found != index.end()) {
    return column(*found->second);
  }
  // A console label: its leading words translated, the rest kept.
  const std::string_view whole(english);
  for (const Row &row : kRows) {
    const std::size_t n = std::strlen(row.en);
    if (n > 1 && row.en[n - 1] == ' ' && whole.size() > n && whole.substr(0, n) == row.en) {
      const std::string key = current_language + "\x1f" + english;
      auto &cache = composed();
      auto [at, inserted] = cache.emplace(key, std::string());
      if (inserted) {
        at->second = std::string(column(row)) + std::string(whole.substr(n));
      }
      return at->second.c_str();
    }
  }
  return english;
}

std::string tr(const std::string &english) { return tr(english.c_str()); }

std::size_t translations() { return sizeof(kRows) / sizeof(kRows[0]); }

bool translation_complete(std::string *missing) {
  for (const Row &row : kRows) {
    if (row.es == nullptr || row.fr == nullptr || *row.es == '\0' || *row.fr == '\0') {
      if (missing != nullptr) {
        *missing = row.en;
      }
      return false;
    }
  }
  return true;
}

} // namespace zoal_atc::gui
