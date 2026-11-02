// Sistema ATM TecBank201D - Version con ventana (Win32 API nativa de Windows)
// No requiere librerias externas: solo windows.h, que ya viene con el sistema.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// ---------------------------------------------------------------- Datos
const int MAX_INTENTOS_PIN = 3;
const int LONGITUD_PIN = 4;
const long long LIMITE_RETIRO_DIARIO = 50000;   // centavos
const long long MULTIPLO_RETIRO = 1000;         // centavos

struct Cuenta {
    int numero;
    string titular;
    string pin;
    long long saldo;          // centavos
    long long retiradoHoy;
    int intentosFallidos;
    bool bloqueada;
};

vector<Cuenta> cuentas = {
    {1001, "Ana Torres",   "1234", 150000, 0, 0, false},
    {1002, "Luis Mendoza", "4321",  80000, 0, 0, false},
    {1003, "Carla Rojas",  "1111",  25050, 0, 0, false},
};

// Matriz de billetes: {denominacion, cantidad}
vector<vector<int>> billetes = {{100, 20}, {50, 40}, {20, 100}, {10, 100}};

// Matriz de movimientos: {fecha, cuenta, tipo, monto, saldo}
vector<vector<string>> movimientos;

int sesion = -1;   // indice de la cuenta autenticada, -1 si no hay sesion

// ---------------------------------------------------------------- Utilidades
string dinero(long long centavos) {
    string ent = to_string(centavos / 100);
    for (int i = static_cast<int>(ent.size()) - 3; i > 0; i -= 3) ent.insert(i, ",");
    ostringstream os;
    os << "$" << ent << "." << setw(2) << setfill('0') << centavos % 100;
    return os.str();
}

string fechaActual() {
    time_t t = time(nullptr);
    tm info{};
    localtime_s(&info, &t);
    char buf[20];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &info);
    return buf;
}

bool soloDigitos(const string &s) {
    return !s.empty() && all_of(s.begin(), s.end(), [](unsigned char c) { return isdigit(c); });
}

string recortar(const string &s) {
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t");
    return a == string::npos ? "" : s.substr(a, b - a + 1);
}

// Monto en centavos; -1 si es invalido
long long parsearMonto(string s) {
    s = recortar(s);
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

int buscarCuenta(int numero) {
    for (size_t i = 0; i < cuentas.size(); ++i)
        if (cuentas[i].numero == numero) return static_cast<int>(i);
    return -1;
}

void registrar(const Cuenta &c, const string &tipo, long long monto) {
    movimientos.push_back({fechaActual(), to_string(c.numero), tipo, dinero(monto), dinero(c.saldo)});
}

long long efectivoDisponible() {
    long long total = 0;
    for (auto &f : billetes) total += static_cast<long long>(f[0]) * f[1];
    return total * 100;
}

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

// ---------------------------------------------------------------- Interfaz
enum Id {
    ID_CUENTA = 101, ID_PIN, ID_INGRESAR, ID_BIENVENIDA, ID_MONTO, ID_DESTINO, ID_PIN_ACT, ID_PIN_NUEVO,
    ID_SALDO, ID_DEPOSITO, ID_RETIRO, ID_TRANSF, ID_MOV, ID_CAMBIAR, ID_SALIR, ID_SALIDA
};

HWND hVentana, hCuenta, hPin, hBienvenida, hMonto, hDestino, hPinAct, hPinNuevo, hSalida;
vector<HWND> panelLogin, panelSesion;
HFONT fuente;

// Tema morado oscuro
const COLORREF COLOR_FONDO = RGB(38, 10, 66);
const COLORREF COLOR_CAMPO = RGB(66, 28, 106);
const COLORREF COLOR_BOTON = RGB(106, 48, 170);
const COLORREF COLOR_BOTON_SOBRE = RGB(138, 78, 205);
const COLORREF COLOR_TEXTO = RGB(240, 230, 255);
HBRUSH pincelFondo, pincelCampo;

HWND crear(const char *clase, const char *texto, DWORD estilo, int x, int y, int w, int h, int id,
           vector<HWND> *panel = nullptr) {
    DWORD ex = (string(clase) == "EDIT") ? WS_EX_CLIENTEDGE : 0;
    if (string(clase) == "BUTTON") estilo |= BS_OWNERDRAW;
    HWND c = CreateWindowExA(ex, clase, texto, WS_CHILD | WS_VISIBLE | estilo, x, y, w, h, hVentana,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessageA(c, WM_SETFONT, reinterpret_cast<WPARAM>(fuente), TRUE);
    if (panel) panel->push_back(c);
    return c;
}

string leerCampo(HWND h) {
    char buf[128];
    GetWindowTextA(h, buf, sizeof(buf));
    return recortar(buf);
}

void salida(const string &texto) { SetWindowTextA(hSalida, texto.c_str()); }

void mostrarPanel(vector<HWND> &panel, bool visible) {
    for (HWND h : panel) ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
}

void limpiarCampos() {
    for (HWND h : {hCuenta, hPin, hMonto, hDestino, hPinAct, hPinNuevo}) SetWindowTextA(h, "");
}

void irALogin() {
    sesion = -1;
    limpiarCampos();
    mostrarPanel(panelSesion, false);
    mostrarPanel(panelLogin, true);
    SetFocus(hCuenta);
}

void irASesion() {
    limpiarCampos();
    SetWindowTextA(hBienvenida, ("Bienvenido(a), " + cuentas[sesion].titular + "  -  Cuenta " +
                                 to_string(cuentas[sesion].numero)).c_str());
    mostrarPanel(panelLogin, false);
    mostrarPanel(panelSesion, true);
    SetFocus(hMonto);
}

// ---------------------------------------------------------------- Operaciones
void ingresar() {
    string txt = leerCampo(hCuenta);
    int idx = (soloDigitos(txt) && txt.size() <= 9) ? buscarCuenta(stoi(txt)) : -1;
    if (idx < 0) { salida("[ERROR] Tarjeta no reconocida."); return; }

    Cuenta &c = cuentas[idx];
    if (c.bloqueada) { salida("[ERROR] Tarjeta bloqueada. Contacte a su banco."); return; }

    if (leerCampo(hPin) == c.pin) {
        c.intentosFallidos = 0;
        sesion = idx;
        irASesion();
        salida("[OK] Acceso concedido. Seleccione una operacion.");
        return;
    }
    c.intentosFallidos++;
    SetWindowTextA(hPin, "");
    if (c.intentosFallidos >= MAX_INTENTOS_PIN) {
        c.bloqueada = true;
        salida("[ALERTA] Tarjeta bloqueada por exceder los intentos permitidos.");
    } else {
        salida("[ERROR] PIN incorrecto. Intentos restantes: " + to_string(MAX_INTENTOS_PIN - c.intentosFallidos));
    }
}

void consultarSaldo() {
    Cuenta &c = cuentas[sesion];
    salida("CONSULTA DE SALDO\r\n\r\nTitular: " + c.titular + "\r\nCuenta: " + to_string(c.numero) +
           "\r\nSaldo: " + dinero(c.saldo) + "\r\nRetiro disponible hoy: " +
           dinero(LIMITE_RETIRO_DIARIO - c.retiradoHoy));
}

void depositar() {
    Cuenta &c = cuentas[sesion];
    long long monto = parsearMonto(leerCampo(hMonto));
    if (monto <= 0) salida("[ERROR] Monto invalido. Debe ser un numero mayor a cero.");
    else if (monto > 1000000) salida("[ERROR] El deposito maximo por operacion es $10,000.00.");
    else {
        c.saldo += monto;
        registrar(c, "Deposito", monto);
        salida("[OK] Deposito exitoso de " + dinero(monto) + "\r\nNuevo saldo: " + dinero(c.saldo));
        SetWindowTextA(hMonto, "");
    }
}

void retirar() {
    Cuenta &c = cuentas[sesion];
    long long monto = parsearMonto(leerCampo(hMonto));
    vector<int> entrega;
    if (monto <= 0) salida("[ERROR] Monto invalido.");
    else if (monto % MULTIPLO_RETIRO != 0) salida("[ERROR] El monto debe ser multiplo de $10.");
    else if (monto > c.saldo) salida("[ERROR] Fondos insuficientes.");
    else if (c.retiradoHoy + monto > LIMITE_RETIRO_DIARIO) salida("[ERROR] Excede el limite diario de retiro.");
    else if (monto > efectivoDisponible()) salida("[ERROR] El cajero no tiene efectivo suficiente.");
    else if (!calcularEntrega(monto, entrega)) salida("[ERROR] El cajero no puede entregar ese monto exacto.");
    else {
        string r = "[OK] Retire su dinero: " + dinero(monto) + "\r\nBilletes entregados:\r\n";
        for (size_t i = 0; i < billetes.size(); ++i) {
            billetes[i][1] -= entrega[i];
            if (entrega[i] > 0) r += "   " + to_string(entrega[i]) + " x $" + to_string(billetes[i][0]) + "\r\n";
        }
        c.saldo -= monto;
        c.retiradoHoy += monto;
        registrar(c, "Retiro", monto);
        salida(r + "Nuevo saldo: " + dinero(c.saldo));
        SetWindowTextA(hMonto, "");
    }
}

void transferir() {
    Cuenta &c = cuentas[sesion];
    string txt = leerCampo(hDestino);
    int idx = (soloDigitos(txt) && txt.size() <= 9) ? buscarCuenta(stoi(txt)) : -1;
    long long monto = parsearMonto(leerCampo(hMonto));
    if (idx < 0) { salida("[ERROR] La cuenta destino no existe."); return; }
    if (cuentas[idx].numero == c.numero) { salida("[ERROR] No puede transferir a su propia cuenta."); return; }
    if (monto <= 0) { salida("[ERROR] Monto invalido."); return; }
    if (monto > c.saldo) { salida("[ERROR] Fondos insuficientes."); return; }

    string pregunta = "Transferir " + dinero(monto) + " a " + cuentas[idx].titular + "?";
    if (MessageBoxA(hVentana, pregunta.c_str(), "Confirmar transferencia", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        salida("[INFO] Operacion cancelada.");
        return;
    }
    c.saldo -= monto;
    cuentas[idx].saldo += monto;
    registrar(c, "Transf. enviada", monto);
    registrar(cuentas[idx], "Transf. recibida", monto);
    salida("[OK] Transferencia realizada: " + dinero(monto) + "\r\nNuevo saldo: " + dinero(c.saldo));
    SetWindowTextA(hMonto, "");
    SetWindowTextA(hDestino, "");
}

void verMovimientos() {
    Cuenta &c = cuentas[sesion];
    string num = to_string(c.numero);
    vector<const vector<string> *> propios;
    for (auto &m : movimientos)
        if (m[1] == num) propios.push_back(&m);

    string r = "ULTIMOS MOVIMIENTOS\r\n\r\n";
    if (propios.empty()) r += "Sin movimientos en esta sesion.";
    else {
        size_t inicio = propios.size() > 10 ? propios.size() - 10 : 0;
        for (size_t i = inicio; i < propios.size(); ++i) {
            const auto &m = *propios[i];
            r += m[0] + "  " + m[2] + "  " + m[3] + "  (saldo " + m[4] + ")\r\n";
        }
    }
    salida(r);
}

void cambiarPin() {
    Cuenta &c = cuentas[sesion];
    string actual = leerCampo(hPinAct), nuevo = leerCampo(hPinNuevo);
    if (actual != c.pin) salida("[ERROR] PIN actual incorrecto.");
    else if (static_cast<int>(nuevo.size()) != LONGITUD_PIN || !soloDigitos(nuevo))
        salida("[ERROR] El PIN nuevo debe tener exactamente 4 digitos.");
    else if (nuevo == c.pin) salida("[ERROR] El nuevo PIN debe ser distinto al actual.");
    else {
        c.pin = nuevo;
        SetWindowTextA(hPinAct, "");
        SetWindowTextA(hPinNuevo, "");
        salida("[OK] PIN actualizado correctamente.");
    }
}

// ---------------------------------------------------------------- Ventana
LRESULT CALLBACK procVentana(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    if (sesion < 0) ingresar();
                    break;
                case ID_INGRESAR: ingresar(); break;
                case ID_SALDO: consultarSaldo(); break;
                case ID_DEPOSITO: depositar(); break;
                case ID_RETIRO: retirar(); break;
                case ID_TRANSF: transferir(); break;
                case ID_MOV: verMovimientos(); break;
                case ID_CAMBIAR: cambiarPin(); break;
                case ID_SALIR:
                    irALogin();
                    salida("Gracias por usar TecBank. Retire su tarjeta.");
                    break;
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            bool campo = msg == WM_CTLCOLOREDIT || reinterpret_cast<HWND>(lp) == hSalida;
            SetTextColor(dc, COLOR_TEXTO);
            SetBkColor(dc, campo ? COLOR_CAMPO : COLOR_FONDO);
            return reinterpret_cast<LRESULT>(campo ? pincelCampo : pincelFondo);
        }
        case WM_DRAWITEM: {
            auto *d = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
            bool presionado = d->itemState & ODS_SELECTED;
            POINT cursor;
            GetCursorPos(&cursor);
            RECT rc;
            GetWindowRect(d->hwndItem, &rc);
            bool encima = PtInRect(&rc, cursor);

            HBRUSH fondo = CreateSolidBrush(presionado ? COLOR_FONDO : (encima ? COLOR_BOTON_SOBRE : COLOR_BOTON));
            FillRect(d->hDC, &d->rcItem, fondo);
            DeleteObject(fondo);
            FrameRect(d->hDC, &d->rcItem, pincelCampo);

            char texto[64];
            GetWindowTextA(d->hwndItem, texto, sizeof(texto));
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, COLOR_TEXTO);
            SelectObject(d->hDC, fuente);
            DrawTextA(d->hDC, texto, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (d->itemState & ODS_FOCUS) {
                RECT foco = d->rcItem;
                InflateRect(&foco, -3, -3);
                DrawFocusRect(d->hDC, &foco);
            }
            return TRUE;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int mostrar) {
    fuente = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    pincelFondo = CreateSolidBrush(COLOR_FONDO);
    pincelCampo = CreateSolidBrush(COLOR_CAMPO);

    WNDCLASSA wc{};
    wc.lpfnWndProc = procVentana;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = pincelFondo;
    wc.lpszClassName = "TecBankATM";
    RegisterClassA(&wc);

    RECT r{0, 0, 560, 480};
    DWORD estilo = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, estilo, FALSE);
    hVentana = CreateWindowExA(0, wc.lpszClassName, "TecBank 201D - Cajero Automatico", estilo, CW_USEDEFAULT,
                               CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);

    crear("STATIC", "TECBANK 201D - ATM", SS_CENTER, 20, 12, 520, 26, 0);

    // Panel de inicio de sesion
    crear("STATIC", "Inserte su tarjeta (numero de cuenta) e ingrese su PIN.", 0, 20, 55, 520, 22, 0, &panelLogin);
    crear("STATIC", "Numero de cuenta:", 0, 100, 100, 165, 22, 0, &panelLogin);
    hCuenta = crear("EDIT", "", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 270, 96, 150, 26, ID_CUENTA, &panelLogin);
    crear("STATIC", "PIN:", 0, 130, 140, 130, 22, 0, &panelLogin);
    hPin = crear("EDIT", "", WS_TABSTOP | ES_PASSWORD | ES_NUMBER | ES_AUTOHSCROLL, 270, 136, 150, 26, ID_PIN, &panelLogin);
    crear("BUTTON", "Ingresar", WS_TABSTOP | BS_DEFPUSHBUTTON, 270, 176, 150, 32, ID_INGRESAR, &panelLogin);
    crear("STATIC", "Cuentas de prueba: 1001, 1002, 1003", 0, 20, 220, 520, 22, 0, &panelLogin);

    // Panel de sesion
    hBienvenida = crear("STATIC", "", 0, 20, 50, 520, 22, ID_BIENVENIDA, &panelSesion);
    crear("STATIC", "Monto $:", 0, 20, 88, 80, 22, 0, &panelSesion);
    hMonto = crear("EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, 105, 84, 120, 26, ID_MONTO, &panelSesion);
    crear("STATIC", "Cuenta destino:", 0, 270, 88, 110, 22, 0, &panelSesion);
    hDestino = crear("EDIT", "", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 385, 84, 120, 26, ID_DESTINO, &panelSesion);
    crear("STATIC", "PIN actual:", 0, 20, 124, 80, 22, 0, &panelSesion);
    hPinAct = crear("EDIT", "", WS_TABSTOP | ES_PASSWORD | ES_NUMBER | ES_AUTOHSCROLL, 105, 120, 120, 26, ID_PIN_ACT, &panelSesion);
    crear("STATIC", "PIN nuevo:", 0, 270, 124, 110, 22, 0, &panelSesion);
    hPinNuevo = crear("EDIT", "", WS_TABSTOP | ES_PASSWORD | ES_NUMBER | ES_AUTOHSCROLL, 385, 120, 120, 26, ID_PIN_NUEVO, &panelSesion);
    crear("BUTTON", "Consultar saldo", WS_TABSTOP, 20, 164, 120, 32, ID_SALDO, &panelSesion);
    crear("BUTTON", "Depositar", WS_TABSTOP, 150, 164, 120, 32, ID_DEPOSITO, &panelSesion);
    crear("BUTTON", "Retirar", WS_TABSTOP, 280, 164, 120, 32, ID_RETIRO, &panelSesion);
    crear("BUTTON", "Transferir", WS_TABSTOP, 410, 164, 120, 32, ID_TRANSF, &panelSesion);
    crear("BUTTON", "Movimientos", WS_TABSTOP, 20, 204, 120, 32, ID_MOV, &panelSesion);
    crear("BUTTON", "Cambiar PIN", WS_TABSTOP, 150, 204, 120, 32, ID_CAMBIAR, &panelSesion);
    crear("BUTTON", "Salir", WS_TABSTOP, 410, 204, 120, 32, ID_SALIR, &panelSesion);

    hSalida = crear("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY, 20, 252, 520, 208, ID_SALIDA);

    irALogin();
    salida("Bienvenido al cajero automatico TecBank 201D.");
    ShowWindow(hVentana, mostrar);
    UpdateWindow(hVentana);

    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageA(hVentana, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    return 0;
}
