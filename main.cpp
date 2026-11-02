// Sistema ATM TecBank201D - Prototipo funcional en C++17
// Simula las operaciones basicas de un cajero automatico:
// autenticacion con PIN, consulta, deposito, retiro, transferencia,
// cambio de PIN y movimientos. Usa vectores, matrices y una interfaz de consola.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <io.h>
#include <windows.h>
#endif

using namespace std;

// ---------------------------------------------------------------- Constantes
const int MAX_INTENTOS_PIN = 3;
const int LONGITUD_PIN = 4;
const long long LIMITE_RETIRO_DIARIO = 50000;   // centavos -> $500.00
const long long MULTIPLO_RETIRO = 1000;         // centavos -> multiplos de $10
const int ANCHO = 52;

// ---------------------------------------------------------------- Estructuras
struct Cuenta {
    int numero;
    string titular;
    string pin;
    long long saldo;          // en centavos para evitar errores de redondeo
    long long retiradoHoy;    // centavos retirados en la sesion del dia
    int intentosFallidos;
    bool bloqueada;
};

// Vector de cuentas registradas en el banco
vector<Cuenta> cuentas = {
    {1001, "Ana Torres",    "1234", 150000, 0, 0, false},
    {1002, "Luis Mendoza",  "4321",  80000, 0, 0, false},
    {1003, "Carla Rojas",   "1111",  25050, 0, 0, false},
};

// Matriz de billetes del cajero: cada fila = {denominacion en $, cantidad}
vector<vector<int>> billetes = {
    {100, 20},
    {50, 40},
    {20, 100},
    {10, 100},
};

// Matriz de movimientos: cada fila = {fecha, cuenta, tipo, monto, saldo}
vector<vector<string>> movimientos;

// ---------------------------------------------------------------- Utilidades
string dinero(long long centavos) {
    bool neg = centavos < 0;
    if (neg) centavos = -centavos;
    string ent = to_string(centavos / 100);
    for (int i = static_cast<int>(ent.size()) - 3; i > 0; i -= 3) ent.insert(i, ",");
    ostringstream os;
    os << (neg ? "-" : "") << "$" << ent << "." << setw(2) << setfill('0') << centavos % 100;
    return os.str();
}

string fechaActual() {
    time_t t = time(nullptr);
    tm info{};
#ifdef _WIN32
    localtime_s(&info, &t);
#else
    localtime_r(&t, &info);
#endif
    char buf[20];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &info);
    return buf;
}

void limpiarPantalla() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void linea(char c = '=') { cout << string(ANCHO, c) << "\n"; }

void titulo(const string &texto) {
    linea();
    int espacios = max(0, (ANCHO - static_cast<int>(texto.size())) / 2);
    cout << string(espacios, ' ') << texto << "\n";
    linea();
}

void pausa() {
    cout << "\nPresione ENTER para continuar...";
    string s;
    getline(cin, s);
}

void mensaje(const string &tipo, const string &texto) {
    cout << "\n [" << tipo << "] " << texto << "\n";
}

string leerLinea(const string &prompt) {
    cout << prompt;
    string s;
    if (!getline(cin, s)) exit(0);
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t");
    return a == string::npos ? "" : s.substr(a, b - a + 1);
}

bool soloDigitos(const string &s) {
    return !s.empty() && all_of(s.begin(), s.end(), [](unsigned char c) { return isdigit(c); });
}

// Lee un entero positivo; devuelve -1 si la entrada no es valida
int leerEntero(const string &prompt) {
    string s = leerLinea(prompt);
    if (!soloDigitos(s) || s.size() > 9) return -1;
    return stoi(s);
}

// Lee un monto (hasta 2 decimales) y lo devuelve en centavos; -1 si es invalido
long long leerMonto(const string &prompt) {
    string s = leerLinea(prompt);
    if (!s.empty() && s[0] == '$') s.erase(0, 1);
    replace(s.begin(), s.end(), ',', '.');
    size_t punto = s.find('.');
    string ent = s.substr(0, punto);
    string dec = punto == string::npos ? "" : s.substr(punto + 1);
    if (!soloDigitos(ent) || ent.size() > 9) return -1;
    if (!dec.empty() && (!soloDigitos(dec) || dec.size() > 2)) return -1;
    while (dec.size() < 2) dec += "0";
    return stoll(ent) * 100 + stoll(dec);
}

// Lee el PIN ocultando los digitos con asteriscos
string leerPin(const string &prompt) {
    cout << prompt;
#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) {
        string linea_pin;
        getline(cin, linea_pin);
        return linea_pin;
    }
    string pin;
    while (true) {
        int c = _getch();
        if (c == '\r' || c == '\n') break;
        if (c == '\b') {
            if (!pin.empty()) { pin.pop_back(); cout << "\b \b"; }
        } else if (isdigit(c) && static_cast<int>(pin.size()) < LONGITUD_PIN) {
            pin += static_cast<char>(c);
            cout << '*';
        }
    }
    cout << "\n";
    return pin;
#else
    string pin;
    getline(cin, pin);
    return pin;
#endif
}

// Busqueda en el vector de cuentas
int buscarCuenta(int numero) {
    for (size_t i = 0; i < cuentas.size(); ++i)
        if (cuentas[i].numero == numero) return static_cast<int>(i);
    return -1;
}

void registrar(const Cuenta &c, const string &tipo, long long monto) {
    movimientos.push_back({fechaActual(), to_string(c.numero), tipo, dinero(monto), dinero(c.saldo)});
}

// ---------------------------------------------------------------- Cajero (hardware simulado)
long long efectivoDisponible() {
    long long total = 0;
    for (auto &f : billetes) total += static_cast<long long>(f[0]) * f[1];
    return total * 100;
}

// Calcula la combinacion de billetes (voraz, mayor denominacion primero).
// Devuelve false si el cajero no puede entregar el monto exacto.
bool calcularEntrega(long long centavos, vector<int> &entrega) {
    long long restante = centavos / 100;
    entrega.assign(billetes.size(), 0);
    for (size_t i = 0; i < billetes.size(); ++i) {
        long long n = min<long long>(restante / billetes[i][0], billetes[i][1]);
        entrega[i] = static_cast<int>(n);
        restante -= n * billetes[i][0];
    }
    return restante == 0;
}

// ---------------------------------------------------------------- Operaciones
void consultarSaldo(const Cuenta &c) {
    limpiarPantalla();
    titulo("CONSULTA DE SALDO");
    cout << " Titular : " << c.titular << "\n";
    cout << " Cuenta  : " << c.numero << "\n";
    cout << " Saldo   : " << dinero(c.saldo) << "\n";
    cout << " Retiro disponible hoy: " << dinero(LIMITE_RETIRO_DIARIO - c.retiradoHoy) << "\n";
    pausa();
}

void depositar(Cuenta &c) {
    limpiarPantalla();
    titulo("DEPOSITO");
    long long monto = leerMonto(" Monto a depositar: $");
    if (monto <= 0) mensaje("ERROR", "Monto invalido. Debe ser un numero mayor a cero.");
    else if (monto > 1000000) mensaje("ERROR", "El deposito maximo por operacion es $10,000.00.");
    else {
        c.saldo += monto;
        registrar(c, "Deposito", monto);
        mensaje("OK", "Deposito exitoso de " + dinero(monto));
        cout << " Nuevo saldo: " << dinero(c.saldo) << "\n";
    }
    pausa();
}

void retirar(Cuenta &c) {
    limpiarPantalla();
    titulo("RETIRO DE EFECTIVO");
    cout << " Saldo actual: " << dinero(c.saldo) << "\n";
    cout << " Solo multiplos de $10. Limite diario: " << dinero(LIMITE_RETIRO_DIARIO) << "\n\n";
    long long monto = leerMonto(" Monto a retirar: $");
    vector<int> entrega;

    if (monto <= 0) mensaje("ERROR", "Monto invalido.");
    else if (monto % MULTIPLO_RETIRO != 0) mensaje("ERROR", "El monto debe ser multiplo de $10.");
    else if (monto > c.saldo) mensaje("ERROR", "Fondos insuficientes.");
    else if (c.retiradoHoy + monto > LIMITE_RETIRO_DIARIO) mensaje("ERROR", "Excede el limite diario de retiro.");
    else if (monto > efectivoDisponible()) mensaje("ERROR", "El cajero no tiene efectivo suficiente.");
    else if (!calcularEntrega(monto, entrega)) mensaje("ERROR", "El cajero no puede entregar ese monto exacto.");
    else {
        for (size_t i = 0; i < billetes.size(); ++i) billetes[i][1] -= entrega[i];
        c.saldo -= monto;
        c.retiradoHoy += monto;
        registrar(c, "Retiro", monto);
        mensaje("OK", "Retire su dinero: " + dinero(monto));
        cout << " Billetes entregados:\n";
        for (size_t i = 0; i < billetes.size(); ++i)
            if (entrega[i] > 0) cout << "   " << entrega[i] << " x $" << billetes[i][0] << "\n";
        cout << " Nuevo saldo: " << dinero(c.saldo) << "\n";
    }
    pausa();
}

void transferir(Cuenta &c) {
    limpiarPantalla();
    titulo("TRANSFERENCIA");
    int destino = leerEntero(" Numero de cuenta destino: ");
    int idx = destino < 0 ? -1 : buscarCuenta(destino);
    if (idx < 0) { mensaje("ERROR", "La cuenta destino no existe."); pausa(); return; }
    if (cuentas[idx].numero == c.numero) { mensaje("ERROR", "No puede transferir a su propia cuenta."); pausa(); return; }

    cout << " Destinatario: " << cuentas[idx].titular << "\n";
    long long monto = leerMonto(" Monto a transferir: $");
    if (monto <= 0) mensaje("ERROR", "Monto invalido.");
    else if (monto > c.saldo) mensaje("ERROR", "Fondos insuficientes.");
    else {
        string conf = leerLinea(" Confirmar transferencia (S/N): ");
        if (conf == "S" || conf == "s") {
            c.saldo -= monto;
            cuentas[idx].saldo += monto;
            registrar(c, "Transf. enviada", monto);
            registrar(cuentas[idx], "Transf. recibida", monto);
            mensaje("OK", "Transferencia realizada: " + dinero(monto));
            cout << " Nuevo saldo: " << dinero(c.saldo) << "\n";
        } else mensaje("INFO", "Operacion cancelada.");
    }
    pausa();
}

void cambiarPin(Cuenta &c) {
    limpiarPantalla();
    titulo("CAMBIO DE PIN");
    if (leerPin(" PIN actual: ") != c.pin) { mensaje("ERROR", "PIN actual incorrecto."); pausa(); return; }
    string nuevo = leerPin(" Nuevo PIN (4 digitos): ");
    if (static_cast<int>(nuevo.size()) != LONGITUD_PIN || !soloDigitos(nuevo)) {
        mensaje("ERROR", "El PIN debe tener exactamente 4 digitos."); pausa(); return;
    }
    if (nuevo == c.pin) { mensaje("ERROR", "El nuevo PIN debe ser distinto al actual."); pausa(); return; }
    if (leerPin(" Confirme el nuevo PIN: ") != nuevo) { mensaje("ERROR", "Los PIN no coinciden."); pausa(); return; }
    c.pin = nuevo;
    mensaje("OK", "PIN actualizado correctamente.");
    pausa();
}

void verMovimientos(const Cuenta &c) {
    limpiarPantalla();
    titulo("ULTIMOS MOVIMIENTOS");
    string num = to_string(c.numero);
    vector<const vector<string> *> propios;
    for (auto &m : movimientos)
        if (m[1] == num) propios.push_back(&m);

    if (propios.empty()) cout << " Sin movimientos en esta sesion.\n";
    else {
        cout << left << setw(17) << " Fecha" << setw(18) << "Tipo" << right << setw(10) << "Monto" << setw(11) << "Saldo" << "\n";
        linea('-');
        size_t inicio = propios.size() > 10 ? propios.size() - 10 : 0;
        for (size_t i = inicio; i < propios.size(); ++i) {
            const auto &m = *propios[i];
            cout << left << " " << setw(17) << m[0] << setw(18) << m[2] << right << setw(10) << m[3] << setw(11) << m[4] << "\n";
        }
    }
    pausa();
}

// ---------------------------------------------------------------- Flujo principal
// Devuelve el indice de la cuenta autenticada o -1
int autenticar() {
    limpiarPantalla();
    titulo("TECBANK 201D - ATM");
    cout << " Bienvenido. Inserte su tarjeta (numero de cuenta).\n";
    cout << " (Cuentas de prueba: 1001, 1002, 1003 | 0 = apagar)\n\n";
    int numero = leerEntero(" Numero de cuenta: ");
    if (numero == 0) return -2;
    int idx = numero < 0 ? -1 : buscarCuenta(numero);
    if (idx < 0) { mensaje("ERROR", "Tarjeta no reconocida."); pausa(); return -1; }

    Cuenta &c = cuentas[idx];
    if (c.bloqueada) { mensaje("ERROR", "Tarjeta bloqueada. Contacte a su banco."); pausa(); return -1; }

    while (c.intentosFallidos < MAX_INTENTOS_PIN) {
        string pin = leerPin(" Ingrese su PIN: ");
        if (pin == c.pin) { c.intentosFallidos = 0; return idx; }
        c.intentosFallidos++;
        int quedan = MAX_INTENTOS_PIN - c.intentosFallidos;
        if (quedan > 0) mensaje("ERROR", "PIN incorrecto. Intentos restantes: " + to_string(quedan));
    }
    c.bloqueada = true;
    mensaje("ALERTA", "Tarjeta bloqueada por exceder los intentos permitidos.");
    pausa();
    return -1;
}

void menuPrincipal(Cuenta &c) {
    while (true) {
        limpiarPantalla();
        titulo("MENU PRINCIPAL");
        cout << " Hola, " << c.titular << "\n\n";
        cout << "  1. Consultar saldo\n";
        cout << "  2. Depositar\n";
        cout << "  3. Retirar efectivo\n";
        cout << "  4. Transferir\n";
        cout << "  5. Ver movimientos\n";
        cout << "  6. Cambiar PIN\n";
        cout << "  7. Salir (retirar tarjeta)\n\n";
        switch (leerEntero(" Seleccione una opcion: ")) {
            case 1: consultarSaldo(c); break;
            case 2: depositar(c); break;
            case 3: retirar(c); break;
            case 4: transferir(c); break;
            case 5: verMovimientos(c); break;
            case 6: cambiarPin(c); break;
            case 7:
                limpiarPantalla();
                titulo("GRACIAS POR USAR TECBANK");
                cout << " Retire su tarjeta. Hasta pronto.\n";
                pausa();
                return;
            default: mensaje("ERROR", "Opcion invalida."); pausa();
        }
    }
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    system("color 5F");   // fondo morado, texto blanco brillante
#endif
    while (true) {
        int idx = autenticar();
        if (idx == -2) break;
        if (idx >= 0) menuPrincipal(cuentas[idx]);
    }
    cout << "\nCajero fuera de servicio.\n";
    return 0;
}
