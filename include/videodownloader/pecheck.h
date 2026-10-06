#ifndef PECHECK_H
#define PECHECK_H

#include <QString>

// Validacion de la cabecera de un ejecutable de Windows SIN ejecutarlo.
//
// Para que existe: si un archivo que no es un programa de 64 bits valido (una descarga cortada,
// una pagina de error guardada como .exe, un archivo recortado) se le pasa a CreateProcess,
// Windows lo toma por un programa de DOS o de 16 bits y abre un cartel del sistema ("Unsupported
// 16-Bit Application", "Bad Image") en el escritorio del usuario. Ese cartel no lo dibuja la
// app y no hay forma de ocultarlo: la unica defensa es no lanzar el archivo.
//
// Solo lee el archivo. El parser es el mismo en todas las plataformas (asi se puede probar en
// cualquiera); quien decide si aplica es el llamador (ToolsUpdater::isRunnableBinary).
namespace PeCheck {

enum class Kind {
    Program, // un .exe que se va a lanzar
    Library  // una .dll que otro programa va a cargar
};

enum class Verdict {
    Valid,     // imagen PE entera de la arquitectura de esta maquina
    Invalid,   // se leyo y NO es un programa valido: no se lanza
    Unreadable // no se pudo leer (abierto en exclusiva por otro proceso, sin permiso): no se
               // sabe. No cuenta como invalido: si la app no lo puede leer, Windows tampoco lo
               // carga, asi que lanzarlo falla sin cartel; darlo por roto tiraria una tool sana.
};

// Valid si `path` es una imagen PE entera de la arquitectura de esta maquina: firma MZ, firma
// PE, maquina y optional header coherentes (x64, o x86 de 32 bits; ARM64 solo en un Windows
// ARM64), y cabeceras y secciones dentro del archivo. Si no es Valid, `reason` trae el motivo
// en ingles, corto, para el log visible.
Verdict inspect(const QString &path, Kind kind = Kind::Program, QString *reason = nullptr);

} // namespace PeCheck

#endif // PECHECK_H
