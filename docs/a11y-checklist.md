# Lista de comprobación manual de accesibilidad

Se repite al cerrar cada hito que toque la UI (UX.md §7). Anota el resultado en ROADMAP.md: lector, versión, sistema y fecha.

## Por cada lector (NVDA, Narrator, VoiceOver, Orca)
- [ ] Al arrancar se anuncian el título de la ventana y la región enfocada.
- [ ] Tab / Shift+Tab recorren todos los controles en orden visual, sin trampas de foco.
- [ ] F6 / Shift+F6 recorren las regiones y cada una se anuncia por su nombre.
- [ ] Botones que solo tienen icono: se anuncia su nombre y su descripción.
- [ ] Árbol de esquemas: nombre, nivel, plegado/desplegado y posición ("3 of 5"); se anuncia "Loading…".
- [ ] Editor: se lee el texto y la línea; al ejecutar se anuncian el inicio y el fin.
- [ ] Grid: se anuncian la cabecera de columna y el valor de la celda; NULL se lee como "null"; se anuncia el total de filas.
- [ ] Errores: se anuncian (assertive) y el detalle técnico se puede abrir y leer.
- [ ] Diálogos: se anuncian el título y el contenido; el foco queda atrapado y vuelve al control de origen al cerrar.
- [ ] Paleta de comandos: se anuncia "N commands" al filtrar y la opción activa al moverse.
- [ ] Marca de producción: se anuncia "PRODUCTION" en la pestaña y en la barra de estado.

## Visual y preferencias del sistema
- [ ] Alto contraste del sistema: la app cambia al tema de alto contraste y no hay texto ilegible.
- [ ] Zoom de texto al 200 %: nada queda cortado ni solapado.
- [ ] Movimiento reducido: no hay animaciones que no sean esenciales.
- [ ] El foco es visible en todos los controles, en los tres temas.
- [ ] Ningún estado se distingue solo por color (prueba con un simulador de daltonismo).

## Teclado de punta a punta
- [ ] Crear conexión → probar → guardar → desplegar el árbol → abrir la tabla → ejecutar → cancelar → copiar resultados, todo sin ratón.
