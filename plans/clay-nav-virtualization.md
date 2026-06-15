# Plan: Virtualización de listas en clay_nav (modelo geométrico único)

> Estado: **pendiente** — implementar en un PR aparte más adelante.
> Repo objetivo: `clay-ps3` (+ bump de submódulo en `ps3-homebrew-template` para el demo).

## Contexto

`clay_nav` resuelve el foco por **geometría**: "todo focusable es un rectángulo; moverse =
saltar al rectángulo más cercano en la dirección pulsada". Hoy lee cada rectángulo con
`Clay_GetElementData(id)`, lo que obliga a **declarar todos los items cada frame**. Para listas
grandes eso es O(N) de layout + medición de texto + dibujo por frame — el cuello de botella
real.

Queremos **virtualización**: declarar a Clay solo la ventana visible, pero que `clay_nav`
pueda navegar **todos** los items. Restricción dura: **un solo modelo mental** (geométrico).
Se descartó la navegación por índice ("lista lógica") porque introduce un segundo modelo con
costuras al cruzar entre la lista y otros elementos (sidebar). Es un **building block
fundacional**: se diseña bien ahora porque un error de base se propaga.

Diseño elegido (Opción A) refinado tras crítica adversarial: **grupo virtual descrito por
datos**, cuyas cajas las calcula `clay_nav` (no el consumidor) anclándose al rectángulo real
del contenedor + offset de scroll. El modelo sigue siendo "rectángulo más cercano"; lo único
nuevo es la *fuente* del rectángulo (Clay para lo declarado, fórmula de clay_nav para lo no
declarado), con la fórmula **centralizada en clay_nav** para que no diverja.

### Hechos de clay.h verificados (críticos)
- `Clay_GetElementData(id)` → `{ .found = false }` (struct en cero) para ids no declarados →
  hay que resolver la caja del foco por predicción cuando está fuera de pantalla (si no, lee
  `{0,0,0,0}`, un rectángulo real equivocado).
- `boundingBox` es la posición **post-childOffset** en pantalla (consistente con el scroll).
- `CLAY_IDI(base,i)` == `Clay_GetElementIdWithIndex(base.stringId, i)` (mismo hash) → clay_nav
  puede reconstruir el id del item i si guarda el `base` (con su `stringId`).
- `CLAY_NAV_MAX_GROUPS = 8` (coincide con la profundidad del stack de scissor del renderer).

## Diseño

### 1. `clay_nav.h` — API nueva (un solo registro por grupo)

```c
#ifndef CLAY_NAV_MAX_GROUPS
#define CLAY_NAV_MAX_GROUPS 8
#endif

/* Geometria de un item en espacio de CONTENIDO, relativa al origen del contenedor
 * (antes de aplicar childOffset). clay_nav la convierte a pantalla. */
typedef struct { float main_pos, main_size, cross_pos, cross_size; } ClayNavItemRect;
typedef void (*ClayNavProvider)(int index, ClayNavItemRect *out, void *user);

typedef struct {
    Clay_ElementId base;       /* CLAY_ID base; conserva stringId para reconstruir ids */
    Clay_ElementId container;  /* contenedor .clip: su box real = origen ground-truth */
    int            count;
    bool           vertical;   /* eje de scroll */
    Clay_Vector2   offset;     /* el childOffset aplicado al contenedor ESTE frame */
    /* via rapida uniforme (provider == NULL): */
    float          content_start;          /* padding inicial sobre el eje de scroll */
    float          item_main, item_gap;    /* tamano + gap sobre el eje de scroll */
    float          cross_pos, cross_size;  /* posicion/tamano en el eje cruzado */
    /* escape hatch no-uniforme: */
    ClayNavProvider provider;  /* NULL => uniforme */
    void           *user;      /* si provider != NULL, debe sobrevivir al foco */
} ClayNavGroup;

void clay_nav_group(ClayNav *nav, ClayNavGroup group);          /* registrar (por frame) */
void clay_nav_focus_virtual(ClayNav *nav, Clay_ElementId base, int index); /* re-anclar */
```

`ClayNav` gana: `ClayNavGroup groups[CLAY_NAV_MAX_GROUPS]; int group_count;` y caché de foco
virtual `bool focused_is_virtual; int focused_index;` (el grupo se re-resuelve por `base` cada
uso, no por índice de slot — evita punteros colgantes).

### 2. `clay_nav.c` — lógica

- **Resolver caja de un item** (helper único, usado por move/wrap/scroll):
  `screen.main = container.box.main + content_start + index*(item_main+item_gap) + offset.main`
  `screen.cross = container.box.cross + cross_pos + offset.cross`
  (con `provider`: `content_pos` viene del callback en vez de la fórmula uniforme). Misma
  fórmula para scoring y scroll → no diverge. Devuelve también el id vía
  `Clay_GetElementIdWithIndex(base.stringId, index)`.
- **Caja del foco** `clay_nav__focus_box`: usa `Clay_GetElementData(focused)` si `found`
  (item visible = ground truth); si no, predice con el grupo cacheado. Solo falla si no hay
  ni declaración ni grupo.
- **Banda visible** `clay_nav__visible_range(group,&first,&last)`: calcula los índices dentro
  del viewport (box del contenedor + offset + métricas) **± margen**. `clay_nav_move` escanea
  solo esa banda del grupo (no `count`) → **O(visible) incluso en el movimiento**. Cruzar
  desde el sidebar cae en un item visible (la banda), evitando saltar a un item lejano fuera
  de pantalla (riesgo R5).
- **`clay_nav_move`**: escanea free items (`Clay_GetElementData`) + la banda visible de cada
  grupo (cajas predichas) → más cercano, mismo scoring. Al elegir virtual, setea la caché.
- **wrap**: además del borde visible, considera explícitamente las cajas predichas de los
  índices `0` y `count-1` del grupo → wrap al primero/último lógico, no al visible.
- **`clay_nav_scroll_into_view`**: usar `clay_nav__focus_box` (predicción incluida) en vez de
  `Clay_GetElementData(focused)`; cambiar el guard de salida (hoy retorna si `!f.found`, que
  con virtualización es el caso normal del destino). **Sin esto el scroll nunca converge**
  (riesgo R1, showstopper).
- **Foco inicial desde grupo**: `clay_nav_group` setea foco al índice 0 si
  `!has_focus && count>0` (respeta orden de declaración, como `clay_nav_add`). `count==0`:
  no contribuye, no setea foco, no llama al provider.
- Compatibilidad: `clay_nav_add`/`move`/`scroll`/`is_focused`/`begin` siguen igual para
  elementos libres; con `group_count==0` el comportamiento es idéntico al actual.

### 3. Contrato de coordenadas (documentar fuerte)
1. El consumidor entrega geometría **en espacio de contenido, relativa al contenedor** (métricas
   uniformes o `ClayNavItemRect`), **nunca coords absolutas**.
2. clay_nav convierte a pantalla con `container.box.origin + content_pos + offset`, **misma
   fórmula** en move y scroll.
3. clay_nav es dueño de aplicar `offset`; el consumidor lo pasa en `ClayNavGroup.offset` y lo
   recibe de `clay_nav_scroll_into_view`.
4. **Test de calibración**: para un item visible, la caja predicha debe ser igual a
   `Clay_GetElementData(item).boundingBox`. Si no, ajustar `content_start`/`cross_pos`.

### 4. `docs/GAMEPAD-NAVIGATION.md`
Reescribir §10 (Scroll): documentar virtualización con `clay_nav_group`, el contrato de
coordenadas, el orden por-frame correcto (`move → scroll_into_view → calcular ventana →
begin → declarar solo la ventana`), y el test de calibración. Nota: la lista de items la
cubre el **grupo** (navegación); la declaración visible es solo para dibujar + `is_focused`
(no se hace `clay_nav_add` por item).

### 5. `ps3-homebrew-template/source/main.c` — demo de referencia
Convertir la lista scrollable de `demo_clay_ui` en **virtualizada**: registrar un
`clay_nav_group` uniforme (count grande, p.ej. 200), calcular `[firstVisible,lastVisible]`
desde el offset y la altura del contenedor, y declarar a Clay **solo** esa ventana (con
`CLAY_IDI("Item", k)` + `clay_nav_is_focused`). El sidebar sigue como elementos libres
(prueba el cruce free↔grupo con un solo modelo). Subir el contador para evidenciar que el
costo por frame no crece con N.

### 6. Consumidores
- `clay-ps3`: commit + push `main`.
- `ps3-homebrew-template`: bump submódulo + demo virtualizado.
- `ps3-remote-play`: solo bump del submódulo (menús cortos no virtualizan; sin cambios de
  comportamiento). Opcional, para alinear versiones.

## Verificación
- **Build (Docker, `--platform linux/amd64`)**: `make clean && make` en template → `src.self`
  y en remote-play → `imgserver.self`, sin errores. (clay-ps3 se valida vía consumidores.)
- **CI**: PRs en verde (`submodules: recursive` ya configurado).
- **Calibración (clave)**: en el demo, comparar la caja predicha de un item visible con
  `Clay_GetElementData(CLAY_IDI("Item",k))` (log temporal o ajuste visual) para confirmar el
  contrato de coordenadas antes de confiar en el scroll de items fuera de pantalla.
- **Conducta en consola (PS3 + PS3LoadX, `--network host`)**:
  - Bajar por la lista mantiene el foco dentro del viewport (scroll lo sigue) y converge sin
    saltos; el recorte (scissor) oculta lo de afuera.
  - Foco puede llegar al item N-1 (no solo a los visibles) y volver; wrap (si se activa) salta
    a primero/último lógico.
  - Cruce sidebar→lista cae en un item visible, no en uno lejano.
  - Subir `count` no degrada el frame rate (solo se declara la ventana).

## Riesgos / mitigaciones (de la crítica adversarial)
1. **Scroll no converge para destino fuera de pantalla (showstopper)** → `scroll_into_view`
   usa caja predicha; cambiar el guard `!found`.
2. **Divergencia de coordenadas predicha vs real** → clay_nav dueño de origen+offset; consumidor
   solo geometría relativa; test de calibración.
3. **Puntero `provider`/`user` colgante en la caché** → la vía uniforme (sin callback) es la
   primaria; el grupo se re-resuelve por `base` cada frame; `user` debe sobrevivir al foco.
4. **Sin foco inicial si solo hay grupo** → `clay_nav_group` setea índice 0.
5. **Item fuera de pantalla "gana" el move al entrar al grupo** → escanear solo la banda
   visible ± margen.
6. **wrap se queda en el borde visible** → considerar índices `0` y `count-1`.
7. **Caché de índice inválido tras mutar la lista** → documentar `clay_nav_focus_virtual`;
   clamp `focused_index` a `[0,count)`.
8. **`count==0`** → guard, no contribuye.
9. **Paridad de id / base dinámico** → guardar `base` completo (con `stringId`); el consumidor
   declara con `CLAY_IDI`/`CLAY_SIDI` usando la misma base.
