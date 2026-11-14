import QtQuick

// Shared by the Qt Quick tests (UX.md section 7): the accessibility walker and tree helpers.
QtObject {
    // Every visible item reachable by Tab must have a role and a non-empty name.
    function problems(root) {
        const found = [];
        const walk = item => {
            if (item.visible && item.activeFocusOnTab) {
                // Qt hides the Accessible.name of a password edit (qquickaccessibleattached_p.h), so
                // the label we gave it is checked through its own properties instead.
                const name = item.Accessible.passwordEdit ? (item.accessibleName || item.Accessible.description) : item.Accessible.name;
                if (!name)
                    found.push(item + " has no Accessible.name");
                if (item.Accessible.role === Accessible.NoRole)
                    found.push(item + " has Accessible.NoRole");
            }
            for (let i = 0; i < item.children.length; ++i)
                walk(item.children[i]);
        };
        walk(root);
        return found;
    }

    function findAll(root, pred) {
        const out = [];
        const walk = item => {
            if (pred(item))
                out.push(item);
            for (let i = 0; i < item.children.length; ++i)
                walk(item.children[i]);
        };
        walk(root);
        return out;
    }

    function isInside(item, ancestor) {
        for (let p = item; p; p = p.parent)
            if (p === ancestor)
                return true;
        return false;
    }
}
