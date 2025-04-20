Vue.prototype.$opts = Vue.observable({});
Vue.set(Vue.prototype.$opts, 0, {});
Vue.set(Vue.prototype.$opts, 1, {});
//// player 1 is along the top, player 2 is along the side
//// stored as a list of rows
Vue.prototype.$grid = Vue.observable({update: false});

Vue.component("options", {
    props: ["pnum"],
    data: function () { return {
        id_counter: 0,
    }},
    computed: {
        opts: function() {
            return this.$opts[this.pnum-1];
        },
    },
    methods: {
        add_option: function (event) {
            const id = this.new_id();
            if (Object.keys(this.$opts[this.pnum-1]).length == 0) {
                this.$set(this.$opts[this.pnum-1], id, {
                    name: "option " + id,
                    probability: 100.0,
                    value: 1.0,
                });
            } else {
                this.$set(this.$opts[this.pnum-1], id, {
                    name: "option " + id,
                    probability: 0.0,
                    value: 1.0,
                });
            }

            //this.$nextTick(() => this.$refs["opt"+id][0].select());
        },
        remove_option: function (remoptid) {
            Vue.delete(this.opts, remoptid);

            let probs = {};
            let prob_sum = 0;
            for (const [optid, opt] of Object.entries(this.opts)) {
                const p = Number(opt.probability);
                probs[optid] = p;
                prob_sum += p;
            }
            const change_needed = 100 - prob_sum;

            if (prob_sum != 0) {
                for (const [optid, opt] of Object.entries(this.opts)) {
                    const p = Number(opt.probability);
                    opt.probability = p + change_needed * p / prob_sum;
                }
            } else {
                const count = Object.entries(this.opts).length;
                for (const [optid, opt] of Object.entries(this.opts)) {
                    const p = Number(opt.probability);
                    opt.probability = change_needed / count;
                }
            }
        },
        new_id: function (event) {
            const id = this.id_counter;
            this.id_counter += 1;
            return id;
        },
        reshape_probs: function (id) { // need to assure probabilities sum to 100
            let probs = {};
            let prob_sum = 0;
            for (const [optid, opt] of Object.entries(this.opts)) {
                const p = Number(opt.probability);
                probs[optid] = p;
                prob_sum += p;
            }
            const change_needed = 100 - prob_sum;
            const can_change_sum = prob_sum - probs[id];

            if (can_change_sum === 0) { // if all other options are zero, we can't scale them, so evenly add to them.
                const n = Object.keys(probs).length - 1;
                if (n === 0) {
                    for (const [optid, opt] of Object.entries(this.opts)) {
                        opt.probability = 100;
                    }
                } else {
                    for (const [optid, opt] of Object.entries(this.opts)) {
                        if (optid != id) {
                            opt.probability = Number(opt.probability) + change_needed / n;
                        }
                    }
                }
            } else { // Scale options
                for (const [optid, opt] of Object.entries(this.opts)) {
                    if (optid != id) {
                        const p = Number(opt.probability);
                        opt.probability = p + change_needed * p / can_change_sum;
                    }
                }
            }
        },
        format: function(val) {
            return Number(val).toFixed(2);
        }
    },
    template: 
`<div>
    <button v-on:click="add_option()">+</button>
    <div v-for="(opt, optid) in $opts[pnum-1]" :key="optid">
        <button v-on:click="remove_option(optid)">-</button>
        <input 
            type=text size=5 :ref="'opt'+optid"  v-model="opt.name"
            :placeholder="\'option \' + optid"
            v-on:change="send_update_options()">
        </input>
        <input style="width:40px;"
            type=number v-model="opt.value"
            placeholder="0">
        </input>
        <input type=range v-on:change="reshape_probs(optid)" min=0 max=100 v-model.lazy="opt.probability"></input>
        {{ format(opt.probability) }}
    </div>
</div>`
});

Vue.component("valuegrid", {
    computed: {
        p1opts: function() {
            return this.$opts[0];
        },
        p2opts: function() {
            return this.$opts[1];
        },
    },
    template: 
`<div>
    <div v-for="(p2opt, p2optid) in p2opts" v-bind:key="p2optid" class="vgrow"> 
        <span v-for="(p1opt, p1optid) in p1opts" v-bind:key="p1opt.id">
            <slot name="mid" :p1optid="p1optid" :p2optid="p2optid"></slot>
        </span>
        <slot name="end" :pnum="2" :optid="p2optid"></slot>
        <span class="vgp2name p2colour">{{ p2opt.name }}</span>
    </div>
    <span v-for="(p, p1optid) in p1opts">
        <slot name="end" :pnum="1" :optid="p1optid"></slot>
    </span>
    <div></div>
    <span v-for="p1opt in p1opts">
        <span class="vgp1name p1colour">{{ p1opt.name }}</span>
    </span>
</div>`
});

Vue.component("playercheck", {
    props: ["p1optid", "p2optid"],
    data: function () { return {
        set: 0, // -1 for p2, 1 for p1
        style: "background-color:lightgray;",
    }},
    methods: {
        onclick: function (event) {
            if (event.button === 0) { // left
                this.set = 1;
                this.style="background:url(./images/downarrow.png),deepskyblue;";
            } else if (event.button === 1) { // middle
                this.set = 0;
                this.style = "background-color:lightgray;";
            } else if (event.button === 2) { // right
                this.set = -1;
                this.style="background:url(./images/rightarrow.png),orange;";
            }

            this.$grid[Number(this.p1optid) * 4096 + Number(this.p2optid)] = Number(this.set);
            Vue.set(this.$grid, "update", !this.$grid.update);
        }
    },
    created: function() {
        this.onclick({button: 1});
    },
    template: 
`<button class="playercheck" :style="style" oncontextmenu="return false;" v-on:mousedown="onclick($event)"></button>`
});

Vue.component("valuespot", {
    props: ["p1optid", "p2optid"],
    data: function () { return {
        colourclass: "",
    }},
    computed: {
        value: function() {
            const _ = this.$grid.update;
            const set = this.$grid[Number(this.p1optid) * 4096 + Number(this.p2optid)];
            const p1_val = this.$opts[0][this.p1optid].value;
            const p2_val = this.$opts[1][this.p2optid].value;
            const value = set == -1 ? -p2_val : set == 1 ? p1_val : 0;
            this.colourclass = value < 0 ? "p2colour" : value > 0 ? "p1colour" : "";
            return Number(value).toFixed(2);
        },
    },
    template: 
`<span :class="'valuespot ' + colourclass">{{ value }}</span>`
});

Vue.component("scaledvaluespot", {
    props: ["p1optid", "p2optid"],
    data: function () { return {
        colourclass: "",
    }},
    computed: {
        value: function() {
            const _ = this.$grid.update;
            const set = this.$grid[Number(this.p1optid) * 4096 + Number(this.p2optid)];
            const p1_opt = this.$opts[0][this.p1optid];
            const p2_opt = this.$opts[1][this.p2optid];
            const value = set == -1 ? -p2_opt.value : set == 1 ? p1_opt.value : 0;
            const scaledval = value * p1_opt.probability * p2_opt.probability / 10000;
            this.colourclass = scaledval < 0 ? "p2colour" : scaledval > 0 ? "p1colour" : "";
            return Number(scaledval).toFixed(2);
        },
    },
    template: 
`<span :class="'valuespot ' + colourclass">{{ value }}</span>`
});

Vue.component("values", {
    computed: {
        value: function() {
            const _ = this.$grid.update;
            var ev = 0;
            for (const [p1optid, p1opt] of Object.entries(this.$opts[0])) {
                for (const [p2optid, p2opt] of Object.entries(this.$opts[1])) {
                    const set = this.$grid[Number(p1optid) * 4096 + Number(p2optid)];
                    let v;
                    if (set === 0) {
                        v = 0;
                    } else if (set === 1) {
                        v = p1opt.value;
                    } else if (set === -1) {
                        v = -p2opt.value;
                    } else {
                        v = 0.0;
                    }

                    ev += v * p1opt.probability * p2opt.probability / 10000;
                }
            }
            return ev.toFixed(2);
        },
    },
    template: `<div> Expected value: {{ value }}<br> </div>`
});

Vue.component("scaledvaluesumspot", {
    props: ["pnum", "optid"],
    data: function () { return {
        colourclass: "",
    }},
    computed: {
        opts: function() {
            return this.$opts[this.pnum-1];
        },
        value: function() {
            const _ = this.$grid.update;
            var sum = 0;
            const otheropts = this.$opts[(this.pnum-1)^1];
            for (const [otheroptid, _] of Object.entries(otheropts)) {
                sum += Number(this.value_of(this.optid, otheroptid));
            }
            this.colourclass = sum < 0 ? "p2colour" : sum > 0 ? "p1colour" : "";
            return sum.toFixed(2);
        },
        cssclass: function() {
            return this.pnum === 1 ? "valuespotbottom" : "valuespotright";
        },
    },
    methods: {
        value_of: function(thisoptid, otheroptid) {
            const [p1optid, p2optid] = this.pnum === 1 ? [thisoptid, otheroptid] : [otheroptid, thisoptid];
            const set = this.$grid[Number(p1optid) * 4096 + Number(p2optid)];
            const p1opts = this.$opts[0][p1optid];
            const p2opts = this.$opts[1][p2optid];
            const value = set == -1 ? -p2opts.value : set == 1 ? p1opts.value : 0;
            const scaledvalue = Number(value * p1opts.probability * p2opts.probability / 10000);
            return scaledvalue;
        }
    },
    template: `<span :class="cssclass + ' ' + colourclass">{{ value }}</span>`
});

Vue.component("valuesumspot", {
    props: ["pnum", "optid"],
    data: function () { return {
        colourclass: "",
    }},
    computed: {
        opts: function() {
            return this.$opts[this.pnum-1];
        },
        value: function() {
            const _ = this.$grid.update;
            var sum = 0;
            const otheropts = this.$opts[(this.pnum-1)^1];
            for (const [otheroptid, _] of Object.entries(otheropts)) {
                sum += Number(this.value_of(this.optid, otheroptid));
            }
            this.colourclass = sum < 0 ? "p2colour" : sum > 0 ? "p1colour" : "";
            return sum.toFixed(2);
        },
        cssclass: function() {
            return this.pnum === 1 ? "valuespotbottom" : "valuespotright";
        },
    },
    methods: {
        value_of: function(thisoptid, otheroptid) {
            const [p1optid, p2optid] = this.pnum === 1 ? [thisoptid, otheroptid] : [otheroptid, thisoptid];
            const set = this.$grid[Number(p1optid) * 4096 + Number(p2optid)];
            const p1_val = this.$opts[0][p1optid].value;
            const p2_val = this.$opts[1][p2optid].value;
            const value = set == -1 ? -p2_val : set == 1 ? p1_val : 0;
            return Number(value);
        }
    },
    template: `<span :class="cssclass + ' ' + colourclass">{{ value }}</span>`
});

var app = new Vue({
    el: "#app",
});
