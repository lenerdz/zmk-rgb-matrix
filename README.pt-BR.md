# ZMK RGB Matrix

[English](./README.md) | **Português (Brasil)**

> Tradução para o português (Brasil) do [README.md](./README.md) deste fork. Em
> caso de divergência, vale o original em inglês.

[![Status do workflow do
GitHub Actions](https://img.shields.io/github/actions/workflow/status/gudzpoz/zmk-rgb-matrix/previews.yml?label=previews)](https://gudzpoz.github.io/zmk-rgb-matrix/)
[![Status do workflow do
GitHub Actions](https://img.shields.io/github/actions/workflow/status/gudzpoz/zmk-rgb-matrix/tests.yml?label=tests)](https://github.com/gudzpoz/zmk-rgb-matrix/actions/workflows/tests.yml)

<table><tbody>
<tr>
<th><center>reactive-gradient</center></th>
<th><center>rainbow-pinwheel</center></th>
<th><center>starlight-smooth</center></th></tr>
<tr>
<td><img alt="reactive-gradient" src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-gradient.gif"></td>
<td><img alt="rainbow-pinwheel" src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-pinwheel.gif"></td>
<td><img alt="starlight-smooth" src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-smooth.gif"></td>
</tr>
</tbody></table>

Um framework extensível de matriz RGB de LEDs, ciente de posição / por tecla,
para o ZMK, com suporte a teclados split e overlays indicadores. Este módulo é
desenvolvido contra o ZMK `main` e o Zephyr 4.1. Veja [Efeitos
embutidos](#efeitos-embutidos) para mais prévias de tudo o que ele traz.

## Instalação

Adicione o repositório ao `config/west.yml` em um repositório de configuração do
ZMK:

```yaml
manifest:
  remotes:
    - name: gudzpoz
      url-base: https://github.com/gudzpoz
  projects:
    - name: zmk-rgb-matrix
      remote: gudzpoz
      revision: main
```

## Uso

Para a matriz RGB funcionar, você precisará fornecer (1) as posições dos LEDs e
(2) as configurações dos efeitos de iluminação em si. Especificar as posições
dos LEDs pode ser particularmente trabalhoso, mas ainda assim é necessário,
então tenha paciência enquanto passamos por isso.

### Definição das posições dos LEDs

Primeiro, siga o guia oficial do ZMK para definir no seu teclado:

- [um `zmk,physical-layout`, descrevendo as posições das teclas com a sua
  propriedade `keys`](https://zmk.dev/docs/hardware-integration/physical-layouts#optional-keys-property)

- e [o hardware da sua fita de
  LED](https://zmk.dev/docs/hardware-integration/lighting/underglow), mas não
  deixe de omitir a etapa `zmk,underglow = &led_strip;`: vamos usar o nosso
  `keypaw,rgb-matrix`, ciente de posição, no lugar do `zmk,underglow` embutido.

Agora, como você provavelmente sabe, os seus LEDs endereçáveis são encadeados
uns nos outros, e a ordem em que são encadeados nem sempre é a mesma das suas
teclas físicas. É por isso que, além de especificar o seu layout físico e a
fita de LED, agora você precisa definir essa ordem exata para a matriz RGB com o
atributo `mapping`:

```dts
#include <dt-bindings/keypaw/rgb_matrix.h>

/ {
    rgb_matrix: rgb-matrix {
        compatible = "keypaw,rgb-matrix";
        strip = <&led_strip>;
        physical-layout = <&default_layout>;
        mapping = <0 1 2 KP_RGB_NO_KEY(350, 50)>;
    };
};
```

> O `&led_strip` e o `&default_layout` na configuração devem corresponder ao que
> você definiu anteriormente. Por exemplo, se você estiver seguindo o guia
> oficial assim:
>
> ```dts
> / {
>     physical_layout0: physical_layout_0 {
>         compatible = "zmk,physical-layout";
>         display-name = "Default Layout";
>     };
> };
> ```
>
> Então você deve usar `&physical_layout0` no lugar de `&default_layout`.

Cada entrada neste `mapping = <0 1 2 KP_RGB_NO_KEY(350, 50)>;` especifica a
posição de um LED: um inteiro `N` significa um LED por tecla sob a `N`-ésima
tecla (`0` significa que esse LED está sob a tecla representada pela primeira
entrada `&key_physical_attrs` no seu layout físico, enquanto `5` significa a
sexta entrada). O tamanho do mapping deve ser igual ao `chain-length` da
fita.

> Use `KP_RGB_NO_KEY(x, y)` para um LED que não está diretamente sob uma tecla;
> as coordenadas usam as unidades do layout (`100` é a largura de uma tecla, de
> forma semelhante ao `&key_physical_attrs`).

> Um central split sem LEDs locais (um dongle USB) deve omitir `strip` e então
> definir explicitamente `mapping = <>;`. Ele não renderiza nada, mas ainda
> comanda os efeitos, os overlays e a sincronização split dos periféricos.

Confuso? Vamos por partes. Primeiro, recomendo usar o [conversor de layout
físico do ZMK](https://zmk-physical-layout-converter.streamlit.app/) para
visualizar a ordem real das suas teclas: abra a página, cole o seu layout do ZMK
no painel "ZMK DTS" e clique em "Update JSON using this"; depois disso você
deverá ver uma visualização com as suas teclas numeradas:

![Exemplo de visualização da ordem das teclas](./docs/key-order.svg)

Agora, pegue o projeto da sua PCB e siga a cadeia de LEDs. Depois sobreponha
(talvez mentalmente) a cadeia ao layout do seu teclado, e é isso que você
precisa para o `mapping` da sua matriz RGB. Por exemplo, a PCB e o layout de
teclado a seguir resultam em <code>mapping = &lt;6 7 8 9 10 11 &nbsp;&nbsp; 23
22 21 20 19 18 &nbsp;&nbsp; 30 31 32 33 34 35 ...&gt;;</code>.

<table><tbody>
<tr><td><img alt="A ordem da cadeia de LEDs em uma PCB" src="./docs/led-order.svg"></td>
<td>→</td>
<td><img alt="A ordem da cadeia de LEDs sobreposta ao layout das teclas" src="./docs/key-in-led-order.svg"></td></tr>
</tbody></table>

> O exemplo acima também tem alguns LEDs sem teclas correspondentes, para os
> quais você precisará calcular as coordenadas por conta própria.
> Aqui está o mapeamento completo do exemplo:
> ```
> / {
>     rgb_matrix: rgb-matrix {
>         compatible = "keypaw,rgb-matrix";
>         strip = <&led_strip>;
>         physical-layout = <&default_layout>;
>
>         mapping = <
>         6 7 8 9 10 11
>         23 22 21 20 19 18
>         30 31 32 33 34 35
>
>         KP_RGB_NO_KEY(1450, 387) // entre 35 e 47
>         KP_RGB_NO_KEY(1350, 387) // entre 34 e 46
>         KP_RGB_NO_KEY(1250, 400) // ~à esquerda do LED anterior
>         KP_RGB_NO_KEY(1180, 350) // ~no canto superior esquerdo do LED anterior
>         47 46
>         KP_RGB_NO_KEY(1000, 500) // ~à direita do 42
>         42
>         KP_RGB_NO_KEY(850, 475)  // ~à esquerda do 42
>         43 44 45
>         >;
>     };
> };
> ```
>
> Um teclado split normalmente declara um nó de matriz e um mapeamento local em
> cada metade. Veja a minha configuração do ZMK para um exemplo bastante
> complexo: [`rgb_matrix` da metade
> esquerda](https://github.com/gudzpoz/keypaw48-zmk/blob/main/boards/shields/keypaw48/keypaw48_left.overlay),
> [`rgb_matrix` da metade
> direita](https://github.com/gudzpoz/keypaw48-zmk/blob/main/boards/shields/keypaw48/keypaw48_right.overlay)
> e [o `physical_layout` ao qual eles são
> mapeados](https://github.com/gudzpoz/keypaw48-zmk/blob/main/boards/shields/keypaw48/keypaw48-layouts.dtsi).

> Se a sua placa tiver várias fitas de LED, você pode usar o driver
> `zmk,led-strip-composite` implementado por este módulo para concatená-las em
> uma única fita, que então pode ser usada na definição da matriz RGB.

### Configuração dos efeitos

Com os LEDs especificados, agora você pode configurar os seus efeitos RGB.
Basicamente, você precisa de um nó de devicetree `keypaw,behavior-rgb-matrix`,
com os efeitos que deseja como filhos:

```dts
/ {
    behaviors {
        kprgb: kprgb {
            compatible = "keypaw,behavior-rgb-matrix";
            #binding-cells = <2>;
            initial-effect = <0>;

            fx_breathe: fx_breathe {
                compatible = "keypaw,rgb-matrix-breathe";
                #binding-cells = <0>;
                color = <0x0000FF>;
                duration = <2400>;
            };
            fx_rainbow: fx_rainbow {
                compatible = "keypaw,rgb-matrix-rainbow";
                #binding-cells = <0>;
                color = <0xFFFFFF>;
                duration = <4000>;
                basis = "x";
                direction = "out";
                palette = "rainbow";
            };
            fx_heatmap: fx_heatmap {
                compatible = "keypaw,rgb-matrix-heatmap";
                #binding-cells = <0>;
                color = <0xFFFFFF>;
                decrease-delay-ms = <25>;
                spread = <150>;
                area-limit = <16>;
                increase-step = <32>;
            };
        };
    };
};
```

> Em builds split, você precisa garantir que as placas recebam as mesmas
> definições de efeitos: um efeito é identificado pela sua posição no registro,
> então um conjunto ou uma ordem diferente dessincroniza as metades. O mesmo
> vale para os overlays: tanto o bit de estado split quanto a ordem de pintura
> vêm da posição de um overlay em `rgb_overlays`, então as duas metades devem
> declarar os mesmos overlays na mesma ordem.

### Configuração opcional de overlays

Opcionalmente, você também pode especificar alguns overlays de LED para indicar
o CapsLock ou a camada ativa no momento, por exemplo. O overlay embutido faz
composição: ele renderiza um efeito enquanto uma condição está ativa e mistura o
resultado sobre os LEDs que ele tem como alvo.

```dts
/ {
    rgb_conditions: rgb_conditions {
        compatible = "keypaw,rgb-conditions";
        // Estado do CapsLock do computador (host)
        cond_caps: cond_caps {
            compatible = "keypaw,rgb-condition-caps-lock";
        };
        // Estado de camada 1 ativa
        cond_layer1: cond_layer1 {
            compatible = "keypaw,rgb-condition-layer";
            layer = <1>;
        };
        // Quando o usuário alterna &tg_batt com bindings de teclas
        cond_batt: cond_batt {
            compatible = "keypaw,rgb-condition-latch";
            tg_batt: tg_batt {
                compatible = "keypaw,behavior-rgb-overlay-toggle";
                #binding-cells = <0>;
            };
        };
    };

    rgb_overlays: rgb_overlays {
        compatible = "keypaw,rgb-overlays";
        // Acende a tecla nº 41 quando o CapsLock está ativo
        caps: caps {
            compatible = "keypaw,rgb-overlay";
            condition = <&cond_caps>;
            keys = <41>;
            fx_caps {
                compatible = "keypaw,rgb-matrix-solid";
                #binding-cells = <0>;
                color = <0x00FFFF>;
            };
        };
        // Acende teclas quando a camada 1 está ativa
        layer_fn1: layer_fn1 {
            compatible = "keypaw,rgb-overlay";
            condition = <&cond_layer1>;
            keys = <13 14 15 16 17 18 19 20 21 22>;
            fx_fn1 {
                compatible = "keypaw,rgb-matrix-solid";
                #binding-cells = <0>;
                color = <0xFF8000>;
            };
        };
        // Um overlay indicador de bateria embutido
        batt: batt {
            compatible = "keypaw,rgb-overlay-battery";
            condition = <&cond_batt>;
            all-leds;
            color = <0x00FF00>;
        };
    };
};
```

Um overlay genérico deve conter exatamente um filho de efeito habilitado (como
acima). Diferentemente dos efeitos "públicos" listados sob o nó `&kprgb`, esses
efeitos filhos "privados" pertencem ao overlay e só podem ser ativados por uma
condição de overlay ativa.

Há mais alguns atributos para o overlay: `opacity = <...>;` ajusta quanto da
saída é misturada nas camadas abaixo; `all-leds;` é uma forma conveniente de
expressar que o efeito é renderizado sobre todos os LEDs (e, alternativamente,
você pode limitar o efeito do overlay a alguns LEDs, especificados com `keys` ou
`leds`).

`keypaw,rgb-conditions` e `keypaw,rgb-overlays` são nós contêiner simples, que
servem de registros. A ordem de declaração de um overlay em `rgb_overlays` é a
sua ordem de pintura: um filho posterior é composto por cima. Todo overlay
registrado é composto por padrão; um efeito selecionável pelo controlador pode
restringir essa lista com os seus próprios `overlays`/`no-overlays`.

> Uma lista explícita `overlays = <&a &b>;` também define a sua própria ordem de
> pintura; posicionamentos duplicados nessa lista são rejeitados.

> A condição latch `cond_batt` é um pouco diferente das outras condições acima:
> ela permite que o usuário alterne a condição por meio de bindings de teclas.
> Basta associar `&tg_batt` em qualquer lugar do keymap.

### Controlando a matriz RGB

O behavior `keypaw,behavior-rgb-matrix` (`&kprgb`) definido acima pode ser usado
para controlar o motor RGB, permitindo ajustar a cor atual, alternar entre
efeitos e ligar e desligar os LEDs. Veja [RGB Underglow
Behavior](https://zmk.dev/docs/keymaps/behaviors/underglow) na documentação do
ZMK para mais detalhes: basta trocar o `&rgb_ug` deles pelo seu `&kprgb`.

```dts
bindings = <
    &kprgb RGB_TOG &fx_solid &fx_reactive
    &kprgb RGB_BRI &kprgb RGB_BRD
    &kprgb RGB_RESET
>;
```

O estado dos efeitos é persistido quando `CONFIG_SETTINGS=y`; caso contrário, o
estado inicial configurado é usado na inicialização. A matriz desliga quando
fica ociosa (idle) por padrão; `CONFIG_KEYPAW_RGB_MATRIX_AUTO_OFF_IDLE=n` a
mantém acesa e aplica `idle-brightness` enquanto estiver ociosa.

> Além dos comandos do ZMK, oferecemos um novo comando `RGB_RESET`. Como
> persistimos os estados por efeito, quando você altera as definições de efeitos
> RGB no devicetree as coisas podem sair de sincronia, e é quando você pode
> querer executar manualmente este comando `&kprgb RGB_RESET`.

## Slots de cor e mapas de cores editáveis

O efeito `static` pode guardar as suas cores por *slot* em vez de por LED. Um mapa
feito assim é igual nas duas metades de um teclado split, pode ser editado
enquanto o teclado funciona, pode ser salvo na flash e é copiado para a metade
periférica. O [ZMK Studio](https://zmk.dev/docs/features/studio) é a forma como as
edições normalmente chegam (veja [Requisições do ZMK
Studio](#requisições-do-zmk-studio)). Um efeito `static` sem `layer-id`
continua fixo, como antes.

### Slots de cor

Um slot é uma posição de uma lista de cores (`slot-colors`) e significa a mesma
coisa nas duas metades, então uma única lista vale para o teclado inteiro. Declare
o tamanho do espaço de slots no nó `keypaw,rgb-matrix` de cada metade, e onde
ficam nele os LEDs que não estão sob uma tecla:

```dts
rgb_matrix: rgb-matrix {
    compatible = "keypaw,rgb-matrix";
    strip = <&led_strip>;
    physical-layout = <&foostan_corne_6col_layout>;
    mapping = < /* como de costume */ >;

    slot-count = <54>;                   /* igual nas duas metades, no máximo 60 */
    no-key-slots = <42 43 44 45 46 47>;  /* a outra metade lista <48 49 50 51 52 53> */
};
```

- O slot de um LED sob uma tecla é a posição dessa tecla no layout físico, então
  `slot-count` é no mínimo o número de teclas.
- `no-key-slots` dá o slot de cada LED que não está sob uma tecla (cada entrada
  `KP_RGB_NO_KEY` do `mapping`), na ordem dessas entradas. Os slots dos LEDs da
  outra metade ficam fora da lista: um slot que nenhum LED local pinta pertence à
  outra metade.
- Cada metade pinta só os slots dos seus próprios LEDs.

Em um Corne, os slots 0 a 41 são as posições das teclas, 42 a 47 o underglow da
esquerda e 48 a 53 o underglow da direita.

### Mapas de cores editáveis

Um mapa é um efeito `static` com `slot-colors` e, para ser editável, `layer-id`:

```dts
map_fn1: fx_ov_fn1 {
    compatible = "keypaw,rgb-matrix-static";
    #binding-cells = <0>;
    layer-id = <1>;
    slot-colors = <0xFF8000 0xFF8000 /* ... uma cor para cada slot ... */>;
};
```

`layer-id` (0 a 14, único entre os mapas) é o id estável da camada do keymap a que
o mapa pertence (`Layer.id` no protocolo do ZMK Studio), e não a posição dela,
então o mapa continua acompanhando a sua camada quando as camadas são reordenadas.
O lugar usual de um mapa é como o efeito de um overlay cuja condição é essa
camada.

Cada mapa guarda três cópias das suas cores:

| Cópia | O que é | Quando muda |
|---|---|---|
| padrão (defaults) | o `slot-colors` do devicetree, na ROM | só com um firmware novo |
| ao vivo (live) | o que os LEDs mostram agora | a cada edição, que repinta na hora |
| salva (saved) | o que foi gravado por último na flash, ou lido dela ao ligar | ao salvar |

- Sem salvar, uma edição dura até o teclado ser desligado.
- Salvar, só na metade central, grava um registro de settings por mapa alterado
  (`keypaw/rgb_matrix/map/<id da camada>`) com apenas os slots que diferem do
  padrão. Um mapa que voltou ao padrão perde o seu registro. Ao ligar, os
  registros são lidos e valem no lugar de `slot-colors`, slot a slot. Um registro
  tem no máximo 2 + 4 x 60 = 242 bytes, abaixo dos 256 de `SETTINGS_MAX_VAL_LEN`.
  Salvar exige `CONFIG_SETTINGS=y` (o ZMK Studio o seleciona); sem ele, salvar
  responde "não suportado".
- Descartar devolve as cores ao vivo para as salvas. Restaurar o padrão de uma
  camada devolve as cores ao vivo dela para `slot-colors` (salve para manter).
- `&kprgb RGB_RESET` e o "Restore Stock Settings" do ZMK Studio apagam todos os
  registros e devolvem todos os mapas ao padrão.

A API em C de tudo isso está em `src/rgb_maps.h`.

### Teclados split

Só a metade central salva. Ela mantém a periférica em dia por slot, com dois
comandos da sincronização split (`CONFIG_KEYPAW_RGB_SPLIT_SYNC`): `RGB_MAP_SET_CMD`
define um slot de um mapa e `RGB_MAP_RESET_CMD` devolve os mapas ao padrão. Cada
edição entra em uma fila e é enviada um comando por vez, com 50 ms entre eles.
Quando a periférica conecta (ou reinicia), a central manda primeiro um reset e
depois só os slots da periférica cuja cor difere do padrão. A periférica nunca
grava na flash: uma periférica desligada volta com os padrões do seu próprio
firmware e recebe as edições de novo quando reconecta. Por isso, depois de mudar
`slot-colors` no devicetree, grave a metade que você mudou.

### Requisições do ZMK Studio

`CONFIG_KEYPAW_RGB_MAPS_STUDIO` (ligada por padrão quando o RPC do ZMK Studio está
no build, ou seja, na metade central) acrescenta seis requisições e uma
notificação ao subsistema `keymap` do ZMK Studio. Elas existem só no fork do
protocolo [lenerdz/zmk-studio-messages](https://github.com/lenerdz/zmk-studio-messages),
cujos ids começam em 100 para não colidirem com os do upstream. Como as
requisições do keymap, todas exigem o teclado desbloqueado.

| Requisição | Id | Resposta | O que faz |
|---|---|---|---|
| `get_led_info` | 100 | `LedInfo`: `slot_count`, `layer_ids` (até 8) e `unsaved_changes` | diz o que pode ser editado |
| `get_led_map` | 101 | `LedMap`: `layer_id` e `colors` (as 54 cores ao vivo, `0xRRGGBB`) | lê um mapa |
| `set_led_colors` | 102 | `OK`, `INVALID_LAYER`, `INVALID_SLOT` ou `BUSY` (qualquer outra falha) | edita até 16 slots de um mapa, tudo ou nada |
| `save_led_changes` | 103 | a mesma resposta do salvamento do keymap: ok, ou `NOT_SUPPORTED`, `NO_SPACE` ou `GENERIC` | grava na flash os mapas alterados |
| `discard_led_changes` | 104 | `true` | devolve as cores ao vivo para as salvas |
| `restore_led_defaults` | 105 | `LedMap`, já restaurado | devolve um mapa ao padrão |

A notificação `unsaved_led_changes_status_changed` (id 100) avisa quando as cores
ao vivo passam a diferir das salvas, ou deixam de diferir. Uma camada sem mapa em
`get_led_map` ou `restore_led_defaults` responde o erro simples `GENERIC`, cujo
valor é 0, então o cliente precisa verificá-lo explicitamente. Um firmware sem
esses handlers responde `RPC_NOT_FOUND` a todas, e é assim que um cliente descobre
que o recurso não existe. O "Restore Stock Settings" do ZMK Studio também
restaura os mapas.

Para usar, troque o projeto `zmk-studio-messages` que o ZMK importa do seu
`app/west.yml` pelo fork, no `config/west.yml` do seu repositório de
configuração (o mesmo nome e caminho), e acrescente o remote:

```yaml
manifest:
  remotes:
    - name: lenerdz
      url-base: https://github.com/lenerdz
  projects:
    - name: zmk-studio-messages
      remote: lenerdz
      revision: <commit>
      path: modules/msgs/zmk-studio-messages
```

O protocolo original não tem nenhuma dessas requisições, então compilar contra
ele falha.

**Limites.** `slot-count` é no máximo 60. As 54 cores do `LedMap` são fixas no
protocolo: com mais slots, aumente `colors max_count` em `keymap.options.in` do
fork (a compilação confere). Os ids de camada vão de 0 a 14, `get_led_info` lista
até 8 mapas e `set_led_colors` leva até 16 slots.

**Buffer de recepção.** O buffer de recepção do ZMK Studio
(`CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE`) tem 30 bytes por padrão e descarta os bytes
que não cabem. Uma requisição que edita 16 cores tem 158 bytes depois do
enquadramento (framing; até cerca de 210 quando muitos dos seus bytes precisam de
escape), então ponha `CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=256` na configuração da
metade central. A compilação falha quando o buffer é menor.

**Situação.** O codec do registro da flash tem testes no computador
(`tests/unit/test_map_codec.py`). O restante compila no CI do
[lenerdz/corne-zmk](https://github.com/lenerdz/corne-zmk) e ainda não foi
experimentado em um teclado real: a edição ao vivo, o salvamento na flash, o
descarte e a cópia para a metade periférica.

## Componentes embutidos

Este módulo do ZMK foi projetado para ser extensível: você pode implementar
efeitos e condições no seu próprio módulo personalizado sem precisar modificar o
código deste módulo. Overlays e gatilhos (triggers) são configurados
inteiramente no devicetree a partir deles. Também buscamos oferecer uma boa
experiência pronta para uso, com vários efeitos, overlays e condições embutidos,
que você pode simplesmente usar sem precisar mexer em nenhum código C.

### Efeitos embutidos

Esses efeitos são agrupados por família de renderização/estado: campos estáticos
(`solid`, `static`), osciladores globais (`breathe`, `spectrum`), campos
espaciais em movimento (`rainbow`, `band`), efeitos reativos (`reactive`,
`ripple`) e efeitos estocásticos (`rain`, `starlight`, `heatmap`,
`digital_rain`).

Toda animação abaixo é renderizada a partir da "placa" `native_sim//zmk_test_mock`
do ZMK. Os links na tabela apontam para o [site da
galeria](https://gudzpoz.github.io/zmk-rgb-matrix/), onde cada variante aparece
ao lado de um trecho de devicetree, pronto para copiar e colar, que a produziu.

<!-- Sem GIFs dentro de links: o GitHub os oculta para pessoas com Accessibility.Motion = Disabled -->

<table>
<thead><tr><th>Efeito</th><th>Descrição</th><th>Atributos</th></tr></thead>
<tbody>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid">solid</a></code></td>
    <td>Uma cor uniforme em todos os LEDs, ou um gradiente estático de matiz varrendo a placa.</td>
    <td><code>axis</code> = <code>none</code> (cor uniforme) |
        <code>vertical</code> (varredura de matiz ao longo de y) |
        <code>horizontal</code> (varredura de matiz ao longo de x)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/solid.gif" width="200" alt="Solid" title="Solid"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid-vertical">vertical</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/solid-vertical.gif" width="200" alt="Solid (vertical)" title="Solid (vertical)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid-horizontal">horizontal</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/solid-horizontal.gif" width="200" alt="Solid (horizontal)" title="Solid (horizontal)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe">breathe</a></code></td>
    <td>Esmaecimento em toda a placa ou um oscilador ciente de posição ao longo de um período.</td>
    <td><code>mode</code> = <code>brightness</code> (BREATHING) |
        <code>river</code> (RIVERFLOW) | <code>hue</code> (HUE_BREATHING) |
        <code>pendulum</code> (HUE_PENDULUM) | <code>wave</code> (HUE_WAVE)<br>
        <code>hue-amplitude</code> (oscilação do matiz em graus para os modos de matiz, padrão 45)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-brightness">brightness</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-brightness.gif" width="200" alt="Breathe (brightness)" title="Breathe (brightness)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-river">river</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-river.gif" width="200" alt="Breathe (river)" title="Breathe (river)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-hue">hue</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-hue.gif" width="200" alt="Breathe (hue)" title="Breathe (hue)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-pendulum">pendulum</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-pendulum.gif" width="200" alt="Breathe (pendulum)" title="Breathe (pendulum)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-wave">wave</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-wave.gif" width="200" alt="Breathe (wave)" title="Breathe (wave)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#spectrum">spectrum</a></code></td>
    <td>A placa inteira percorre a roda de matiz de forma global (QMK <code>CYCLE_ALL</code>).</td>
    <td>&mdash;</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#spectrum">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/spectrum.gif" width="200" alt="Spectrum" title="Spectrum">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive">reactive</a></code></td>
    <td>Acende uma tecla pressionada e a apaga gradualmente; o formato aceso é configurável.</td>
    <td><code>spread</code> = <code>point</code> (somente a tecla) |
        <code>disc</code> (círculo preenchido) | <code>cross</code> (linha + coluna) |
        <code>nexus</code> (tudo exceto a cruz, com decaimento radial)<br>
        <code>spread-radius</code> (raio de disc/nexus, em unidades do layout; para nexus é a largura da faixa medida para fora a partir da cruz excluída)<br>
        <code>background-brightness</code> (brilho dos LEDs apagados, em porcentagem)<br>
        <code>multi</code> (acumula vários pressionamentos em vez de apenas o mais recente)<br>
        <code>palette</code> = <code>solid</code> (matiz do usuário) |
        <code>gradient</code> (matiz baseado na posição) |
        <code>complement</code> (o fundo apagado mantém o matiz, uma tecla pressionada pisca com o matiz oposto)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-point">point</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-point.gif" width="200" alt="Reactive (point)" title="Reactive (point)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-disc">disc</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-disc.gif" width="200" alt="Reactive (disc)" title="Reactive (disc)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-cross">cross</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-cross.gif" width="200" alt="Reactive (cross)" title="Reactive (cross)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-nexus">nexus</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-nexus.gif" width="200" alt="Reactive (nexus)" title="Reactive (nexus)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-gradient">gradient</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-gradient.gif" width="200" alt="Reactive (gradient)" title="Reactive (gradient)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-complement">complement</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-complement.gif" width="200" alt="Reactive (complement)" title="Reactive (complement)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-multi">multi</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-multi.gif" width="200" alt="Reactive (multi)" title="Reactive (multi)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#ripple">ripple</a></code></td>
    <td>Ondulações radiais se expandem a partir de cada tecla pressionada sobre um fundo apagado.</td>
    <td><code>background-brightness</code> (brilho do fundo apagado, em porcentagem)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#ripple">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/ripple.gif" width="200" alt="Ripple" title="Ripple">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow">rainbow</a></code></td>
    <td>Gradiente de arco-íris ciente de posição, com base espacial e direção selecionáveis.</td>
    <td><code>basis</code> = <code>x</code> | <code>y</code> | <code>radial</code> |
        <code>pinwheel</code> | <code>spiral</code> | <code>chevron</code> |
        <code>flag</code>
        (coordenada espacial na qual a cor é mapeada; <code>flag</code> é uma
        varredura da esquerda para a direita inclinada por fileira, de modo que ondula)<br>
        <code>direction</code> = <code>out</code> | <code>in</code> (inverso) |
        <code>dual</code> (dois centros) | <code>bloom</code> (espelha cada metade, com basis x)<br>
        <code>palette</code> = <code>rainbow</code> (roda de matiz completa) |
        <code>solid</code> (matiz único, faixa de brilho em deslocamento)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-x">x</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-x.gif" width="200" alt="Rainbow (x)" title="Rainbow (x)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-y">y</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-y.gif" width="200" alt="Rainbow (y)" title="Rainbow (y)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-radial">radial</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-radial.gif" width="200" alt="Rainbow (radial)" title="Rainbow (radial)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-pinwheel">pinwheel</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-pinwheel.gif" width="200" alt="Rainbow (pinwheel)" title="Rainbow (pinwheel)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-spiral">spiral</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-spiral.gif" width="200" alt="Rainbow (spiral)" title="Rainbow (spiral)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-chevron">chevron</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-chevron.gif" width="200" alt="Rainbow (chevron)" title="Rainbow (chevron)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-flag">flag</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-flag.gif" width="200" alt="Rainbow (flag)" title="Rainbow (flag)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-in">in</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-in.gif" width="200" alt="Rainbow (in)" title="Rainbow (in)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-dual">dual</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-dual.gif" width="200" alt="Rainbow (dual)" title="Rainbow (dual)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-bloom">bloom</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-bloom.gif" width="200" alt="Rainbow (bloom)" title="Rainbow (bloom)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-solid">solid</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-solid.gif" width="200" alt="Rainbow (solid)" title="Rainbow (solid)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band">band</a></code></td>
    <td>Uma placa de matiz único com uma faixa de saturação ou de brilho em movimento.</td>
    <td><code>channel</code> = <code>sat</code> (esmaece a saturação) |
        <code>val</code> (esmaece o brilho)<br>
        <code>shape</code> = <code>linear</code> (rolagem) |
        <code>pinwheel</code> (rotação) | <code>spiral</code> (avança em espiral para fora)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-saturation-linear">saturação, linear</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-saturation-linear.gif" width="200" alt="Band (saturation, linear)" title="Band (saturation, linear)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-saturation-pinwheel">saturação, pinwheel</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-saturation-pinwheel.gif" width="200" alt="Band (saturation, pinwheel)" title="Band (saturation, pinwheel)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-saturation-spiral">saturação, spiral</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-saturation-spiral.gif" width="200" alt="Band (saturation, spiral)" title="Band (saturation, spiral)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-value-linear">valor, linear</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-value-linear.gif" width="200" alt="Band (value, linear)" title="Band (value, linear)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#static">static</a></code></td>
    <td>Mapa fixo de cores por LED (QMK <code>ALPHAS_MODS</code> e padrões semelhantes); cada LED
        recebe uma cor do array <code>slot-colors</code> (por slot de cor) ou, sem ele,
        do array <code>led-colors</code> (por LED).</td>
    <td><code>slot-colors</code> (valores RGB, um por slot de cor; exige <code>slot-count</code> no nó
        <code>keypaw,rgb-matrix</code>), <code>layer-id</code> (torna o mapa editável em tempo de execução, veja
        <a href="#slots-de-cor-e-mapas-de-cores-editáveis">Slots de cor e mapas de cores editáveis</a>),
        <code>led-colors</code> (valores RGB, um por LED pelo índice da cadeia, usado só sem
        <code>slot-colors</code>; LEDs além do fim permanecem apagados)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#static">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/static.gif" width="200" alt="Static" title="Static">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain">rain</a></code></td>
    <td>Teclas acesas aleatoriamente com cores aleatórias (QMK <code>PIXEL_RAIN</code>, <code>PIXEL_FLOW</code>,
        <code>RAINDROPS</code>, <code>JELLYBEAN_RAINDROPS</code>, <code>PIXEL_FRACTAL</code>).</td>
    <td><code>mode</code> = <code>pixel</code> (teclas aleatórias, matizes aleatórios) |
        <code>drops</code> (a cada passo desloca levemente o matiz de uma tecla aleatória; a placa permanece acesa, então precisa de uma cor saturada) |
        <code>jellybean</code> (também randomiza a saturação) |
        <code>flow</code> (cursor ao longo da cadeia) |
        <code>fractal</code> (matiz único pulsa a partir do centro)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-pixel">pixel</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-pixel.gif" width="200" alt="Rain (pixel)" title="Rain (pixel)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-flow">flow</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-flow.gif" width="200" alt="Rain (flow)" title="Rain (flow)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-drops">drops</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-drops.gif" width="200" alt="Rain (drops)" title="Rain (drops)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-jellybean">jellybean</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-jellybean.gif" width="200" alt="Rain (jellybean)" title="Rain (jellybean)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-fractal">fractal</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-fractal.gif" width="200" alt="Rain (fractal)" title="Rain (fractal)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight">starlight</a></code></td>
    <td>LEDs acendem e apagam aleatoriamente com brilho variável, mantendo a cor do usuário.</td>
    <td><code>smooth</code> (rampa o brilho em vez de alternar) |
        <code>dual-hue</code> (varia o matiz em &plusmn;30) |
        <code>dual-sat</code> (varia a saturação em &plusmn;30)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight.gif" width="200" alt="Starlight" title="Starlight"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight-smooth">smooth</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-smooth.gif" width="200" alt="Starlight (smooth)" title="Starlight (smooth)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight-dual-hue">dual hue</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-dual-hue.gif" width="200" alt="Starlight (dual hue)" title="Starlight (dual hue)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight-dual-sat">dual sat</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-dual-sat.gif" width="200" alt="Starlight (dual sat)" title="Starlight (dual sat)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#heatmap">heatmap</a></code></td>
    <td>Mapa de calor por tecla dos pressionamentos recentes, decaindo com o tempo (QMK <code>TYPING_HEATMAP</code>).</td>
    <td><code>decrease-delay-ms</code> (ms ocioso antes de perder um tom de calor)<br>
        <code>spread</code> (raio de aquecimento dos vizinhos, em unidades do layout)<br>
        <code>area-limit</code> (limite, por pressionamento, do calor que um vizinho recebe)<br>
        <code>increase-step</code> (tons adicionados por pressionamento de tecla)<br>
        <code>slim</code> (aquece apenas a tecla pressionada, não os vizinhos)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#heatmap">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/heatmap.gif" width="200" alt="Heatmap" title="Heatmap"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#heatmap-slim">slim</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/heatmap-slim.gif" width="200" alt="Heatmap (slim)" title="Heatmap (slim)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#digital-rain">digital_rain</a></code></td>
    <td>Colunas caindo no estilo Matrix: uma cabeça cai por coluna deixando uma cauda verde que se apaga.</td>
    <td><code>color</code> (matiz da cauda; verde por padrão) |
        <code>duration</code> (velocidade de queda; menor é mais rápido)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#digital-rain">padrão</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/digital-rain.gif" width="200" alt="Digital Rain" title="Digital Rain">
</td></tr>
</tbody>
</table>

Todo efeito aceita `color` e `duration`. Efeitos selecionáveis também aceitam
`overlays` e `no-overlays`. `RGB_EFF`/`RGB_EFR` percorrem todos os efeitos
selecionáveis habilitados; os efeitos privados de overlay não fazem parte dessa
lista.

### Condições e overlays embutidos

| Componente | Compatible | Observações |
|---|---|---|
| Condição sempre ativa | `keypaw,rgb-condition-always` | / |
| Condição de Caps Lock | `keypaw,rgb-condition-caps-lock` | Ativa enquanto o host informa Caps Lock |
| Condição de camada | `keypaw,rgb-condition-layer` | Ativa enquanto uma camada do keymap está ativa |
| Condição de BLE | `keypaw,rgb-condition-ble` | Somente no central; corresponde ao índice do perfil BLE ativo e ao estado da conexão |
| Condição latch | `keypaw,rgb-condition-latch` | Com estado; alternada pelo behavior aninhado sob ela |
| Condição And | `keypaw,rgb-condition-and` | Todas as condições listadas ativas |
| Condição Or | `keypaw,rgb-condition-or` | Qualquer uma das condições listadas ativa |
| Alternador de overlay | `keypaw,behavior-rgb-overlay-toggle` | Filho de `keypaw,rgb-condition-latch`. Nome do nó ≤ 8 caracteres |
| Indicador de bateria | `keypaw,rgb-overlay-battery` | Barra de nível a partir da carga desta metade |

O compositor genérico `keypaw,rgb-overlay` é o outro tipo de overlay: ele
renderiza um efeito enquanto a sua condição é verdadeira.

## Efeitos e condições personalizados

Módulos de terceiros podem adicionar efeitos, condições e overlays sem editar
este módulo; overlays e gatilhos são então configurados no devicetree a partir
deles. O guia completo de criação está em
[`docs/extending.md`](./docs/extending.md) (em inglês).

Como uma amostra rápida, um módulo de efeito de terceiros inclui a API pública
como `<zmk/rgb_matrix.h>`, fornece um binding que inclui
`keypaw,rgb-matrix-effect-common.yaml`, embute
`struct kp_rgb_effect_common_config` e `struct kp_rgb_effect_common_data` como o
primeiro membro das estruturas de configuração e de dados do efeito, e
instancia cada nó de devicetree habilitado com `KP_RGB_EFFECT_DEFINE()`.

O `modules/zmk-rgb-effect-example` no repositório pai
[`keypaw48-zmk`](https://github.com/gudzpoz/keypaw48-zmk/tree/main/modules/zmk-rgb-effect-example)
é um exemplo completo de módulo separado. Ele demonstra o binding necessário, o
layout de dados, o renderizador e o nó filho de `&kprgb`, sem usar headers
internos da matriz.

## Licença

MIT. Veja [LICENSE](./LICENSE).
